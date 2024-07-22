#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "aodv.h"
#include "nowqtt_common.h"
#include "nowqtt_client_common.h"

nowqtt_entity_t** nowqtt_entity_list;
size_t nowqtt_entity_count = 0;
const char* nowqtt_device_config;

void send_log(const char* logmsg){
    put_in_send_queue(LOG, strlen(logmsg), (uint8_t*)logmsg, (uint8_t)0);
}

void update_state(nowqtt_entity_t* entity){
    put_in_send_queue(STATE, strlen(entity->state), (uint8_t*)entity->state, entity->id);
}

void update_float_state(nowqtt_entity_t* entity, float data){
    int n = snprintf(entity->state, SENSOR_STATE_MAX_SIZE, "%.2f", data);
    if(n < SENSOR_STATE_MAX_SIZE && n > 0){
        put_in_send_queue(STATE, strlen(entity->state), (uint8_t*)entity->state, entity->id);
    }
}

void update_int_state(nowqtt_entity_t* entity, int data){
    int n = snprintf(entity->state, SENSOR_STATE_MAX_SIZE, "%i", data);
    if(n < SENSOR_STATE_MAX_SIZE && n > 0){
        put_in_send_queue(STATE, strlen(entity->state), (uint8_t*)entity->state, entity->id);
    }
}

void update_char_state(nowqtt_entity_t* entity, const char* data){
    int n = snprintf(entity->state, SENSOR_STATE_MAX_SIZE, "%s", data);
    if(n < SENSOR_STATE_MAX_SIZE && n > 0){
        put_in_send_queue(STATE, strlen(entity->state), (uint8_t*)entity->state, entity->id);
    }
}

void send_influx(const char* influxmsg){
    put_in_send_queue(INFLUX, strlen(influxmsg), (uint8_t*)influxmsg, 0);
}

static void send_config(nowqtt_entity_t* entity){
    uint8_t buff[MAX_PAYLOAD_LEN];
    size_t conf_len = strlen(entity->constructor);
    size_t device_len = strlen(nowqtt_device_config);
    size_t paylod_len =  conf_len + device_len;
    if(paylod_len > (MAX_PAYLOAD_LEN)){
        return;
    }
    memcpy(buff, entity->constructor, conf_len);
    memcpy(buff + conf_len, nowqtt_device_config, device_len);
    put_in_send_queue(CONFIG, paylod_len, buff, entity->id);
}

void init_sensors(){
    for(int i = 0; i < nowqtt_entity_count; i++){
        send_config(nowqtt_entity_list[i]);
    }
    for(int i = 0; i < nowqtt_entity_count; i++){
        if(nowqtt_entity_list[i]->state[0] == 0)continue;
        update_state(nowqtt_entity_list[i]);
    } 
}

void process_rx_data(void *pvParameters){
    uint8_t data[MAX_PAYLOAD_LEN];
    msg_header_t* header = (msg_header_t*)data;
    char* payload = (char*)&data[sizeof(msg_header_t)];
    size_t payload_size;

    for(;;){
        get_from_receive_queue(&payload_size, data);
        payload_size -= sizeof(msg_header_t);

        if(header->sensor_id <= nowqtt_entity_count){
            switch(header->command){
                case HANDSHAKE:
                    break;

                case COMMAND:
                    if(header->sensor_id == 0)break;
                    if(payload_size > 0 && payload_size < SENSOR_STATE_MAX_SIZE && nowqtt_entity_list[header->sensor_id - 1]->has_command_topic == 1 && payload[payload_size - 1] == '\0'){
                        nowqtt_entity_list[header->sensor_id - 1]->handler(payload, payload_size);
                    }
                    break;

                case RESET:
                    if(header->sensor_id == 0){
                        init_sensors();
                        break;
                    }
                    send_config(nowqtt_entity_list[header->sensor_id - 1]);
                    if(nowqtt_entity_list[header->sensor_id- 1]->state[0] == 0)break;
                    update_state(nowqtt_entity_list[header->sensor_id - 1]);
                    break;
            }
        }
    }
    vTaskDelete(NULL);
}