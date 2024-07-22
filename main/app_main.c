#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "nowqtt_client.h"
#include "esp_now_sec_key.h"

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static void switch_h(const char* state, size_t len);

// const char* mqtt_device_config = ",\"dev\":{\"ids\":\"Relay 1\",\"name\":\"Relay 1\",\"sw\":\"1.0.0\"}}";
// nowqtt_entity_t generic_switch =  {"h/switch/nowqtt/relay1/c|{\"name\":\"Relay 1\"", true, "ON", switch_h};

const char* mqtt_device_config = ",\"dev\":{\"ids\":\"Relay 2\",\"name\":\"Relay 2\",\"sw\":\"1.0.0\"}}";
nowqtt_entity_t generic_switch =  {"h/switch/nowqtt/relay2/c|{\"name\":\"Relay 2\"", true, "ON", switch_h};

// const char* mqtt_device_config = ",\"dev\":{\"ids\":\"Relay 3\",\"name\":\"Relay 3\",\"sw\":\"1.0.0\"}}";
// nowqtt_entity_t generic_switch =  {"h/switch/nowqtt/relay3/c|{\"name\":\"Relay 3\"", true, "ON", switch_h};

nowqtt_entity_t *nowqtt_devices[] = {&generic_switch};

//\\\\\\\\\\\\\\\//
  //IO Code
//\\\\\\\\\\\\\\\//

#define SWITCH_PIN 15

static void init_switch_io(){
    gpio_reset_pin(SWITCH_PIN);
    gpio_set_pull_mode(SWITCH_PIN, GPIO_FLOATING);
    gpio_set_direction(SWITCH_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SWITCH_PIN, 1);
} 


static void switch_h(const char* state, size_t len){
    if(state[1] == 'N'){//ON
        gpio_set_level(SWITCH_PIN, 1);
        update_char_state(&generic_switch, "ON");
    }else if(state[1] == 'F'){//OFF
        gpio_set_level(SWITCH_PIN, 0);
        update_char_state(&generic_switch, "OFF");
    }
}


void app_main()
{
    esp_log_level_set("*", ESP_LOG_NONE);
    // esp_log_level_set("aodv", ESP_LOG_DEBUG);
    // esp_log_level_set("aodv_peering", ESP_LOG_DEBUG);
    // esp_log_level_set("aodv_routing", ESP_LOG_DEBUG);

    // vTaskDelay(pdMS_TO_TICKS(3000));
    // for(;;){
    //     ESP_LOGI("main", "Staring up");
    //     vTaskDelay(pdMS_TO_TICKS(3000));  
    // }

    init_switch_io();

    nowqtt_init(mqtt_device_config, nowqtt_devices, sizeof(nowqtt_devices)/sizeof(*nowqtt_devices), enc_key, 10, true, gateway_esp_mac);

    // send_log("Generic 1 ESP started!");
    send_log("Generic 2 ESP started!");
    // send_log("Generic 3 ESP started!");
}