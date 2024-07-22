#pragma once

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_now.h"

typedef uint8_t (mac_addr_t)[6];

typedef void (*aodv_rx_cb)(const mac_addr_t sender, const uint8_t *data, size_t data_len, uint8_t serviceId);

#define MAX_PAYLOAD_LEN ESP_NOW_MAX_DATA_LEN - 13 //forward header size

typedef struct{
    MessageBufferHandle_t buffer;
    uint8_t ID;
    aodv_rx_cb callback;
    SemaphoreHandle_t qos_handle;
}aodv_service_handle;

typedef struct{
    esp_now_peer_info_t default_peer_conf;
    uint32_t retransmitt_count;
} aodv_config_t;

extern mac_addr_t own_mac;

aodv_service_handle* aodv_register_service(const aodv_rx_cb rx_callback, const size_t buffer_size, const uint8_t service_id);

void aodv_send(const aodv_service_handle* handle, const mac_addr_t dest, const uint8_t* data, const size_t len, const bool qos);

void aodv_trace(const aodv_service_handle* handle, const mac_addr_t dest, bool new_route);

void aodv_register_trace_callback(const aodv_rx_cb callback);

void aodv_init(aodv_config_t config);