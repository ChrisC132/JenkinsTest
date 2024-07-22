#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"

#include "hal/gpio_types.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "driver/gpio.h"

#include "aodv.h"
#include "aodv_routing.h"
#include "aodv_ota.h"

mac_addr_t esp2 = {0xc0, 0x4e, 0x30, 0x4b, 0x31, 0x1a};
mac_addr_t esp_rx = {0x80, 0x65, 0x99, 0x4b, 0x34, 0x7c};
mac_addr_t selfe;

static void setup_pin(gpio_num_t gpio, gpio_pull_mode_t pull, gpio_mode_t dir, uint32_t level){
    gpio_reset_pin(gpio);
    gpio_set_pull_mode(gpio, pull);
    gpio_set_direction(gpio, dir);
    gpio_set_level(gpio, level);
}

static void data_cb(const mac_addr_t sender, const uint8_t *data, size_t data_len){
    ESP_LOGI("main", "Rx Msg: %s", (char*)(data));
}

static void ping_cb(const mac_addr_t sender, const uint8_t *data, size_t data_len){
    int len = data_len / 7;
    //len = ((int8_t*)data)[data_len -1];
    ESP_LOGI("main", "Got Trace Back with %i Hops", len);
    
    for(int i = 0; i < len; i++){
        int rssi = *((int8_t*)((data + (i * 7) + 6)));
        ESP_LOGI("main", "Hop: %i RSSI: %i dBm", i + 1, rssi);
        ESP_LOG_BUFFER_HEX_LEVEL("main", data + (i*7), 6, ESP_LOG_INFO);
    }
}

esp_now_peer_info_t peer_conf = {
    .channel = 2,
    .encrypt = true,
    .ifidx = WIFI_IF_STA,
    .lmk = "kcbmsiznl97sbnd",
};

aodv_config_t aodv_conf = {
    .retransmitt_count = 5,
};

void app_main()
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI("main", "Starting Up");
    //esp_log_level_set("aodv", ESP_LOG_DEBUG);
    //esp_log_level_set("aodv_peering", ESP_LOG_DEBUG);
    //esp_log_level_set("aodv_routing", ESP_LOG_DEBUG);
    //esp_log_level_set("espnow_ota", ESP_LOG_DEBUG);

    aodv_conf.default_peer_conf = peer_conf;
    esp_efuse_mac_get_default((uint8_t*)selfe);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }

    //setup_pin(GPIO_NUM_15, GPIO_PULLDOWN_ONLY, GPIO_MODE_OUTPUT, 0);

    aodv_init(aodv_conf);
    aodv_service_handle* aodv_handle = aodv_register_service(data_cb, 1024, 1);
    ESP_LOGI("main", "ID of Handle:%i", (int)aodv_handle->ID);

    aodv_ota_init(0);


    if(aodv_handle == NULL)ESP_LOGI("main", "aodv_handle init fail!");

    
    aodv_register_trace_callback(ping_cb);
    for(;;){
        aodv_trace(aodv_handle, esp_rx, true);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    return;

    // if(memcmp(selfe, esp1, sizeof(mac_addr_t)) != 0){
    //     for(;;){  
    //         gpio_set_level(GPIO_NUM_15, 1);
    //         vTaskDelay(pdMS_TO_TICKS(1000));
    //         gpio_set_level(GPIO_NUM_15, 0);
    //         vTaskDelay(pdMS_TO_TICKS(100));
    //     }
    // }

    uint8_t buff[] = "Hallo OTH-Regensburg!";
    int len = sizeof(buff);
    uint32_t count = 0;
    for(;;){
        count++;
        aodv_send(aodv_handle, esp2, buff, len, true);
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI("main", "Sent %i messages", (int)(count));
    }
} 