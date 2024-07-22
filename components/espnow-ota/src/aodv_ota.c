#include "aodv_ota.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/FreeRTOSConfig_arch.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"

#include "esp_ota_ops.h"
#include "esp_now.h"
#include "esp_log.h"

#include "aodv.h"
#include "aodv_peering.h"

#define OTA_DATA_PACKET_SIZE 232
#define MAX_REREQUEST_COUNT 20

static const char *TAG = "espnow_ota";

typedef struct{
    uint8_t type;
    uint32_t packet_num;
}__attribute__((packed)) ota_msg_header_t;

typedef struct{
    uint8_t type;
    uint32_t total_size;
}__attribute__((packed)) ota_init_msg_t;

typedef struct{
    uint8_t type;
    uint32_t packet_num;
}__attribute__((packed)) ota_requeest_retrans_msg_t;

MessageBufferHandle_t ota_msg_buff_h = NULL;
ota_init_msg_t init_msg;
TaskHandle_t ota_task_handle = NULL;
aodv_service_handle* ota_aodv_h;
mac_addr_t ota_patner_mac = {0};

static void ota_thread(void *pvParameters){
    esp_ota_handle_t handle;
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    uint32_t packet_count = init_msg.total_size / OTA_DATA_PACKET_SIZE; //calculate number of packets
    if((init_msg.total_size % OTA_DATA_PACKET_SIZE) != 0) packet_count++;
    ESP_LOGD(TAG, "OTA Thread Started expecting %i packets", (int)packet_count);
    
    bool packet_track[packet_count];
    memset(packet_track, 0, packet_count);

    if(update_partition == NULL)goto ABORT;
    if(esp_ota_begin(update_partition, init_msg.total_size, &handle) != ESP_OK)goto ABORT;
    ota_msg_buff_h = xMessageBufferCreate(2048);
    if(ota_msg_buff_h == NULL)goto ABORT;

    uint8_t ready_msg = OTA_READY;
    aodv_send(ota_aodv_h, ota_patner_mac, &ready_msg, sizeof(ready_msg), true);

    uint32_t rerequest_count = 0;
    uint32_t rxCount = 0;

    for(;;){

        uint8_t buff[ESP_NOW_MAX_DATA_LEN];
        ota_msg_header_t* header = (ota_msg_header_t*)buff;
        size_t len = xMessageBufferReceive(ota_msg_buff_h, buff, ESP_NOW_MAX_DATA_LEN, pdMS_TO_TICKS(2000));

        if(len == 0){//Remote stoped sending check if everything is here else request retransmissions
            bool done = true;
            ota_requeest_retrans_msg_t request;
            request.type = OTA_RETRANS;
            ESP_LOGD(TAG, "Rerequesting");
            rerequest_count++;
            if(rerequest_count > MAX_REREQUEST_COUNT || rxCount < (packet_count / MAX_REREQUEST_COUNT)){
                ESP_LOGD(TAG, "Aborting max rerequest count reached or no packets received");
                goto ABORT; 
            }
            for(int i = 0; i < packet_count; i++){
                if(packet_track[i])continue;
                done = false;
                request.packet_num = i;
                aodv_send(ota_aodv_h, ota_patner_mac, (uint8_t*)&request, sizeof(ota_requeest_retrans_msg_t), false);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            if(done == false)continue;
            if(esp_ota_end(handle) != ESP_OK){
                ESP_LOGD(TAG, "Aborting Ota End failed");
                goto ABORT; 
            }
            ESP_LOGD(TAG, "Done rebooting");
            esp_ota_set_boot_partition(update_partition);
            esp_restart();
            vTaskDelete(NULL);
        }

        if(header->packet_num >= packet_count){
            ESP_LOGD(TAG, "Aborting Packet count not in range");
           goto ABORT; 
        }
        rxCount++;
        esp_ota_write_with_offset(handle, &buff[sizeof(ota_msg_header_t)], OTA_DATA_PACKET_SIZE, header->packet_num*OTA_DATA_PACKET_SIZE);
        packet_track[header->packet_num] = true;
    }

ABORT:
    esp_ota_abort(handle);
    if(ota_msg_buff_h != NULL)vMessageBufferDelete(ota_msg_buff_h);
    ota_msg_buff_h = NULL;
    ota_task_handle = NULL;
    vTaskDelete(NULL);
}

// static bool is_discover_known(uint32_t id){
//     static uint32_t brd_sig_ring_buff[OTA_DISCOVER_CACHE]= {0};
//     static uint32_t brd_sig_cnt = 0;

//     for(int i = 0; i < OTA_DISCOVER_CACHE; i++){
//         if(brd_sig_ring_buff[i] == id){
//             return true;
//         }
//     }
//     // add request if unknown
//     brd_sig_cnt = (brd_sig_cnt + 1) % OTA_DISCOVER_CACHE;
//     brd_sig_ring_buff[brd_sig_cnt] = id;
//     return false;
// }

void process_ota_msg(const mac_addr_t sender, const uint8_t* buff, size_t size, uint8_t service){
    if(buff[0] == OTA_DATA){
        if(ota_msg_buff_h == NULL)return;
        xMessageBufferSend(ota_msg_buff_h, buff, size, 0);
    }

    // if(buff[0] == OTA_DISCOVER){
    //     ota_discover_msg_t* msg = (ota_discover_msg_t*)buff;
    //     if(is_discover_known(msg->id) == false){
    //         //forward_to_all_peers(buff, size); //todo forward function 
    //         ota_discover_response_msg_t response;
    //         response.type = OTA_DISCOVER_RESPOONSE;
    //         memcpy(response.id, ota_id, sizeof(ota_identifier_t));
    //         send_data_qos(sender, (uint8_t*)&response, sizeof(ota_discover_response_msg_t));
    //     }
    // }

    if(buff[0] == OTA_INIT){
        ESP_LOGD(TAG, "Got Init Message");
        if(ota_task_handle != NULL)return;
        if(size != sizeof(ota_init_msg_t))return;
        memcpy(&init_msg, buff, size);
        memcpy(ota_patner_mac, sender, sizeof(mac_addr_t));
        if(xTaskCreate(ota_thread, "ota_task", configMINIMAL_STACK_SIZE*10, NULL, tskIDLE_PRIORITY + 1, &ota_task_handle) != pdPASS){
            ota_task_handle = NULL;
            return;
        }
    }
}

void aodv_ota_init(uint8_t service_ID){
    esp_ota_mark_app_valid_cancel_rollback();
    ota_aodv_h = aodv_register_service(process_ota_msg, 512, 0);
    ESP_LOGI(TAG, "Ota Init Done");
}