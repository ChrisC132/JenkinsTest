#include "aodv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "esp_random.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "nvs_flash.h"

#include "aodv_msg_types.h"
#include "aodv_routing.h"
#include "aodv_peering.h"

#define GARBAGE_INTERVALL CONFIG_AODV_MAINTENANCE_INTERVALL * 1000

static const char *TAG = "aodv";
static uint8_t rx_buffer_storage[CONFIG_AODV_RX_BUFFER_SIZE];
aodv_service_handle* service_array[MAX_SERVICE_COUNT]={0};

QueueHandle_t aodv_event_queue_h;
StaticMessageBuffer_t rx_buffer_struct;
MessageBufferHandle_t rx_buff_h;
aodv_config_t config;
mac_addr_t own_mac;
TaskHandle_t aodv_tsk_h;
aodv_rx_cb trace_cb = NULL;
TimerHandle_t garbage_timer_h;

typedef enum AODV_EVENTS {RX_MESSAGE, TX_MESSAGE, MAINTENANCE, REQUEST_ROUTE, GET_TRACE} aodv_event_type_t;

typedef struct{
    uint8_t event;
    uint8_t service;
} aodv_queue_t;

aodv_service_handle* aodv_register_service(const aodv_rx_cb rx_callback, const size_t buffer_size, const uint8_t service_id){
    if(service_id >= MAX_SERVICE_COUNT) return NULL; //check if service id is in Range
    if(service_array[service_id] != NULL) return NULL; //check is Service isn't initialized yet

    aodv_service_handle* new_service = (aodv_service_handle*)malloc(sizeof(aodv_service_handle));
    new_service->ID = service_id;
    new_service->callback = rx_callback;
    new_service->buffer = xMessageBufferCreate(buffer_size);
    new_service->qos_handle = xSemaphoreCreateBinary();

    if(new_service->buffer == NULL || new_service->qos_handle == NULL){ //check if init was sucessfull
        vMessageBufferDelete(new_service->buffer);
        vSemaphoreDelete(new_service->qos_handle);
        free(new_service);
        return NULL;
    }

    service_array[service_id] = new_service;
    return service_array[service_id];
}

void aodv_register_trace_callback(const aodv_rx_cb callback){
    trace_cb = callback;
}

static void generic_send(const aodv_service_handle* handle, const forward_msg_header_t* header, const uint8_t* data, const size_t len){
    xMessageBufferSend(handle->buffer, header, sizeof(forward_msg_header_t), portMAX_DELAY);
    xMessageBufferSend(handle->buffer, data, len,portMAX_DELAY);
    aodv_queue_t to_queue = {TX_MESSAGE,handle->ID};
    xQueueSend(aodv_event_queue_h, &to_queue, 0);
    xTaskNotifyGive(aodv_tsk_h);
}

void aodv_send(const aodv_service_handle* handle, const mac_addr_t dest, const uint8_t* data, const size_t len, const bool qos){
    if(len > MAX_PAYLOAD_LEN || len == 0)return;
    if(handle == NULL)return;

    forward_msg_header_t header;
    memcpy(header.dst_mac, dest, sizeof(mac_addr_t));
    memcpy(header.src_mac, own_mac, sizeof(mac_addr_t));
    
    if(qos == false){
        header.type = MAX_INTERNAL + DATA + (handle->ID * MAX_DATA);
        generic_send(handle, &header, data, len);
        return;
    }

    header.type = MAX_INTERNAL + DATA_QOS + (handle->ID * MAX_DATA);
    xSemaphoreTake(handle->qos_handle, 0);//clear semaphore
    for(int i = 0; i < config.retransmitt_count; i++){
        generic_send(handle, &header, data, len);
        if(xSemaphoreTake(handle->qos_handle, 60)){
            return;
        }
        if(i == (config.retransmitt_count/4)*3){
            xMessageBufferSend(handle->buffer, dest, sizeof(mac_addr_t), portMAX_DELAY);
            aodv_queue_t to_queue = {REQUEST_ROUTE,handle->ID};
            xQueueSend(aodv_event_queue_h, &to_queue, 0);
            xTaskNotifyGive(aodv_tsk_h);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
}

void aodv_trace(const aodv_service_handle* handle, const mac_addr_t dest, bool new_route){
    //TO_DO Make gerneral function for new route
    aodv_queue_t to_queue;
    if(new_route){
        xMessageBufferSend(handle->buffer, dest, sizeof(mac_addr_t), portMAX_DELAY);
        to_queue.event = REQUEST_ROUTE;
        to_queue.event = handle->ID;
        xQueueSend(aodv_event_queue_h, &to_queue, 0);
        xTaskNotifyGive(aodv_tsk_h);
        vTaskDelay(pdMS_TO_TICKS(45));
    }
    

    forward_msg_header_t header;
    header.type = TRACEREQ;
    memcpy(header.dst_mac, dest, sizeof(mac_addr_t));
    memcpy(header.src_mac, own_mac, sizeof(mac_addr_t));
    
    xMessageBufferSend(handle->buffer, &header, sizeof(forward_msg_header_t), portMAX_DELAY);
    to_queue.event = GET_TRACE;
    to_queue.service = handle->ID;
    xQueueSend(aodv_event_queue_h, &to_queue, 0);
    xTaskNotifyGive(aodv_tsk_h);
}

static void process_trace(uint8_t* buff, int8_t rssi, size_t len){
    forward_msg_header_t* header = (forward_msg_header_t*) buff;
    len += sizeof(mac_addr_t) + sizeof(int8_t);
    if(len > ESP_NOW_MAX_DATA_LEN)return;
    memcpy(&buff[len - (sizeof(mac_addr_t) + sizeof(int8_t))], own_mac, sizeof(mac_addr_t));
    memcpy(&buff[len - sizeof(int8_t)], &rssi, sizeof(int8_t));
    len += get_route_info(header->dst_mac, &buff[len]);

    if(memcmp(header->dst_mac, own_mac, sizeof(mac_addr_t)) == 0){
        if(header->type == TRACEREP){
            if(trace_cb == NULL)return;
            trace_cb(header->src_mac, buff + sizeof(forward_msg_header_t), len - sizeof(forward_msg_header_t), 0xFF);
            return;
        } 
        header->type = TRACEREP;
        memcpy(header->dst_mac, header->src_mac, sizeof(mac_addr_t));
        memcpy(header->src_mac, own_mac, sizeof(mac_addr_t));
    }

    forward_message(buff, len);
}

static void process_data_msg(uint8_t* buff, size_t len){
    //extract Service and Type
    uint8_t type = buff[0] % MAX_DATA;
    uint8_t service = (buff[0] - MAX_INTERNAL) / MAX_DATA;
    
    if(service_array[service] == NULL){
        ESP_LOGD(TAG, "Processing DataMessage ID: %i -->NOT FOUND, Length: %i, ", (int)buff[0], (int)len);
        return; //service not initialzed
    }

    if(type <= DATA_QOS){
        ESP_LOGD(TAG, "Processing DataMessage ID: %i Length: %i, ", (int)buff[0], (int)len);
        if(len <= sizeof(forward_msg_header_t))
            return; //msg has no payload
        
        forward_msg_header_t* header = (forward_msg_header_t*) buff;
        service_array[service]->callback(header->src_mac, &buff[sizeof(forward_msg_header_t)], len - sizeof(forward_msg_header_t), service);
        if(type == DATA)
            return;

        //Msg is QoS so send ACK
        ESP_LOGD(TAG, "Sending ACK");
        header->type = buff[0] + (DATA_ACK - DATA_QOS);
        memcpy(header->dst_mac, header->src_mac, sizeof(mac_addr_t));
        memcpy(header->src_mac, own_mac, sizeof(mac_addr_t));
        forward_message(buff, sizeof(forward_msg_header_t));
        return;
    }
    if(type == DATA_ACK){
        ESP_LOGD(TAG, "Got ACK");
        xSemaphoreGive(service_array[service]->qos_handle);
        return;
    }
}

static void maintenance_timer_cb(TimerHandle_t xTimer){
    aodv_queue_t to_queue = {MAINTENANCE,0};
    xQueueSend(aodv_event_queue_h, &to_queue, 0);
    xTaskNotifyGive(aodv_tsk_h);
}

static void process_reset(const uint8_t* buff, size_t len){
    xTimerStop(garbage_timer_h, portMAX_DELAY);
    forward_to_all_peers(buff, len);
    if(buff[1] > 1){
        reset_peer_table();
    }
    if(buff[1] > 0){
        reset_route_table();
    }
    vTaskDelay(pdMS_TO_TICKS(
        ((uint32_t)((((double)esp_random())/UINT32_MAX)*30000))+30000
        ));
    xMessageBufferReset(rx_buff_h);
    xTimerStart(garbage_timer_h, portMAX_DELAY);
}

static void process_rx_msg(){
    uint8_t buff[ESP_NOW_MAX_DATA_LEN];
    size_t len = xMessageBufferReceive(rx_buff_h, buff, ESP_NOW_MAX_DATA_LEN, 0);
    mac_addr_t sender;
    xMessageBufferReceive(rx_buff_h, sender, sizeof(mac_addr_t), 0);
    int8_t rssi;
    xMessageBufferReceive(rx_buff_h, &rssi, 1, 0);


    if(buff[0] > MAX_INTERNAL){
        if(memcmp(&buff[1], own_mac, sizeof(mac_addr_t)) != 0){
            ESP_LOGD(TAG, "Forwarding Message ID:%i, RSSI:%i", (int)buff[0], (int)rssi);
            forward_message(buff, len);
            return;
        }
        process_data_msg(buff, len);
        return;
    }

    ESP_LOGD(TAG, "Processing RXMessage ID:%i, RSSI:%i", (int)buff[0], (int)rssi);

    if(buff[0] < MAX_PEERING){
        process_peering_msg(buff, sender, rssi, len);
        return;
    }

    if(buff[0] < MAX_ROUTING){
        process_routing_msg(buff, sender);
        return;
    }
        
    if(buff[0] < MAX_TRACE){
        process_trace(buff, rssi, len);
        return;
    }

    if(buff[0] < MAX_RESET){
        process_reset(buff, len);
        return;
    }
}

static void process_tx_msg(MessageBufferHandle_t buffer){
    uint8_t buff[ESP_NOW_MAX_DATA_LEN];
    size_t len = xMessageBufferReceive(buffer, buff, sizeof(forward_msg_header_t), 0);
    len += xMessageBufferReceive(buffer, buff + sizeof(forward_msg_header_t), MAX_PAYLOAD_LEN, 0);
    if(len < sizeof(forward_msg_header_t))return; //check if there is a forward header
    ESP_LOGD(TAG, "Processing TXMessage ID:%i", (int)buff[0]);
    forward_message(buff, len);
}

static void aodv_task(){
    garbage_timer_h = xTimerCreate("garbagetimer", pdMS_TO_TICKS(GARBAGE_INTERVALL), pdTRUE, NULL, maintenance_timer_cb);
    xTimerStart(garbage_timer_h, 0);
    for(;;){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        aodv_queue_t from_queue;
        while(xQueueReceive(aodv_event_queue_h, &from_queue, 0) == pdTRUE){
            if(from_queue.event == MAINTENANCE){
                ESP_LOGD(TAG, "Running Maintenance");
                peering_maintenance();
                routing_maintenance();
                continue;
            }

            if(from_queue.event == RX_MESSAGE){
                process_rx_msg();
                continue;
            }

            if(from_queue.event == TX_MESSAGE){
                process_tx_msg(service_array[from_queue.service]->buffer);
                continue;
            }

            if(from_queue.event == REQUEST_ROUTE){
                mac_addr_t dest;
                size_t len = xMessageBufferReceive(service_array[from_queue.service]->buffer, &dest, sizeof(mac_addr_t), 0);
                if(len != sizeof(mac_addr_t))continue;
                ESP_LOGD(TAG, "Requesting Route");
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, dest, sizeof(mac_addr_t), ESP_LOG_DEBUG);
                request_new_route(dest);
                continue;
            }

            if(from_queue.event == GET_TRACE){
                ESP_LOGD(TAG, "Requesting Traceroute");
                uint8_t buff[sizeof(forward_msg_header_t)];
                if(xMessageBufferReceive(service_array[from_queue.service]->buffer, buff, sizeof(forward_msg_header_t), 0) != sizeof(forward_msg_header_t))continue;
                forward_message(buff, sizeof(forward_msg_header_t));
                continue;
            }
        }
    }
}

static void espnow_receive_handler(const esp_now_recv_info_t *rx_info, const uint8_t *data, int size){
    if(rx_info->des_addr[0] == 0xFF && data[0] > MAX_BRDCST)return; //Drop if a broadcast is received but it shouldnt be a broadcast to filter unencrypted messages for security purposes
    
    if(xMessageBufferSpaceAvailable(rx_buff_h) < size + sizeof(mac_addr_t) + sizeof(int8_t))return; //drop if message rx buffer is full
    int8_t rssi = rx_info->rx_ctrl->rssi;
    xMessageBufferSend(rx_buff_h, data, size, 0);
    xMessageBufferSend(rx_buff_h, rx_info->src_addr, sizeof(mac_addr_t), 0);
    xMessageBufferSend(rx_buff_h, &rssi, sizeof(int8_t), 0); //first byte of rx_ctrl is rssi value

    aodv_queue_t to_queue = {RX_MESSAGE,0};
    xQueueSend(aodv_event_queue_h, &to_queue, 0);
    xTaskNotifyGive(aodv_tsk_h);
}

void aodv_init(aodv_config_t conf){
    ESP_LOGD(TAG, "Setting up AODV Rouing");
    //save config and init message buffers
    config = conf;
    rx_buff_h = xMessageBufferCreateStatic(sizeof(rx_buffer_storage), rx_buffer_storage, &rx_buffer_struct);
    aodv_event_queue_h = xQueueCreate(128, sizeof(aodv_queue_t));

    //check if nvs is initialized before starting wifi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }

    //init wifi hardware
    esp_event_loop_create_default();
    esp_netif_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_start();
    esp_wifi_set_channel(config.default_peer_conf.channel, WIFI_SECOND_CHAN_NONE);
    esp_efuse_mac_get_default((uint8_t*)own_mac);
    ESP_LOGI(TAG, "Mac of this ESP:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, own_mac, sizeof(mac_addr_t), ESP_LOG_INFO);

    //init espnow
    esp_now_init();
    esp_now_register_recv_cb(espnow_receive_handler);
        esp_now_peer_info_t broadcast_peer = {
        .ifidx = config.default_peer_conf.ifidx,
        .peer_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    };
    esp_now_add_peer(&broadcast_peer);

    //start thread
    ESP_LOGD(TAG, "Spawning relevant Threads");
    init_peering(conf.default_peer_conf);
    xTaskCreate(aodv_task, "mngmnt_thread", 3072, NULL, tskIDLE_PRIORITY + 2, &aodv_tsk_h);
    vTaskDelay(pdMS_TO_TICKS(500));
}