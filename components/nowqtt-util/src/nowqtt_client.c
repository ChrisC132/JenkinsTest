#include "nowqtt_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "aodv.h"
#include "nowqtt_common.h"
#include "nowqtt_client_common.h"

uint32_t hatbeat_sec;

static void hartbeat_thread(void *pvParameters)
{   
    for(;;){ 
        put_in_send_queue(HANDSHAKE, 0, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(hatbeat_sec * 1000));
    }
}

void nowqtt_init(const char* device_config, nowqtt_entity_t** entitys, size_t len, uint8_t* sec_key, uint32_t heartbeat_sek, bool retransmitt, mac_addr_t bridge_addr)
{
    hatbeat_sec = heartbeat_sek;
    
    nowqtt_entity_list = entitys;
    nowqtt_device_config = device_config;
    nowqtt_entity_count = len;

    for(int i = 0; i < nowqtt_entity_count ; i++){
        nowqtt_entity_list[i]->id = i + 1;
    }

    nowqtt_comm_init(retransmitt, bridge_addr, sec_key);

    init_sensors();
    xTaskCreate(hartbeat_thread, "hartbeat_thread", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(process_rx_data, "process_rx_data", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 2, NULL);
}