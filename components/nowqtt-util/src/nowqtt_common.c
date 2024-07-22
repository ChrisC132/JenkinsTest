#include "nowqtt_common.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_crc.h"
#include "esp_mac.h"

#include "aodv.h"
#include "aodv_ota.h"

static uint8_t rx_buffer[2048];
StaticMessageBuffer_t rx_buffer_str;
MessageBufferHandle_t rx_buff_handle;
aodv_service_handle* nowqtt_aodv_h;

bool send_qos = false;
mac_addr_t destination;

static void data_cb(const mac_addr_t sender, const uint8_t *data, size_t data_len, uint8_t service){
    xMessageBufferSend(rx_buff_handle, data, data_len, 0);
}

esp_now_peer_info_t peer_conf = {
    .channel = 8,
    .encrypt = true,
    .ifidx = WIFI_IF_STA,
};

aodv_config_t aodv_conf = {
    .retransmitt_count = 10,
};

void put_in_send_queue(commands_t type, size_t len, const uint8_t* data , uint8_t id){
    if(len > MAX_PAYLOAD_LEN - sizeof(msg_header_t))return;
    uint8_t packet[MAX_PAYLOAD_LEN];
    msg_header_t* header = (msg_header_t*)packet;
    header->command = (uint8_t)type;
    header->sensor_id = id;
    memcpy(&packet[sizeof(msg_header_t)], data, len);

    if(send_qos){
        aodv_send(nowqtt_aodv_h, destination, packet, len + sizeof(msg_header_t), true);
        return;
    }
    aodv_send(nowqtt_aodv_h, destination, packet, len + sizeof(msg_header_t), false);

}

void get_from_receive_queue(size_t* payload_size, uint8_t* payload){
    *payload_size = xMessageBufferReceive(rx_buff_handle, payload, MAX_PAYLOAD_LEN, portMAX_DELAY);
}

void nowqtt_comm_init(bool retransmitt, mac_addr_t bridge_mac, uint8_t* key){
    send_qos = retransmitt;
    memcpy(destination, bridge_mac, sizeof(mac_addr_t));
    rx_buff_handle = xMessageBufferCreateStatic(sizeof(rx_buffer), rx_buffer, &rx_buffer_str);

    memcpy(peer_conf.lmk, key, sizeof(peer_conf.lmk));
    aodv_conf.default_peer_conf = peer_conf;

    aodv_init(aodv_conf);
    aodv_ota_init(0);
    nowqtt_aodv_h = aodv_register_service(data_cb, 512, 1);
}