#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#include "esp_err.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"


#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "sdkconfig.h"

#include "aodv.h"
#include "aodv_msg_types.h"
#include "aodv_peering.h"

//#define ESP_NOW_MAX_ENCRYPT_PEER_NUM 17

typedef struct{
    uint8_t type;
    mac_addr_t src_mac;
} peer_msg_t;

typedef struct{
    esp_now_peer_info_t peer_info;
    bool active;
    uint8_t age;
} aodv_peer_t;

static const char *TAG = "aodv_peering";
static const mac_addr_t brdcst_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

esp_now_peer_info_t default_peer_conf;

aodv_peer_t peerlist[ESP_NOW_MAX_ENCRYPT_PEER_NUM] = {0};
uint32_t peer_cnt= 0;

peer_msg_t request = {
    .type = PREQ,
};

static aodv_peer_t* get_peer(const mac_addr_t peer_adr){
    if(esp_now_is_peer_exist(peer_adr) == false)return NULL;
    for(int i = 0; i < ESP_NOW_MAX_ENCRYPT_PEER_NUM; i++){
        if(peerlist[i].active == false)continue;
        if(memcmp(peerlist[i].peer_info.peer_addr, peer_adr, sizeof(mac_addr_t)) == 0){
            return &peerlist[i];
        }
    }
    return NULL;
}

static bool add_peer(const mac_addr_t mac){
    if(esp_now_is_peer_exist(mac))return false; //dont add if it already exists
    ESP_LOGD(TAG, "Adding Peer");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, mac, 6, ESP_LOG_DEBUG);
    int i = 0;
    for(; i < ESP_NOW_MAX_ENCRYPT_PEER_NUM; i++){
        if(peerlist[i].active == false)
            break;
    }
    if(peerlist[i].active == true)
        return false;
    peerlist[i].peer_info = default_peer_conf;
    memcpy(peerlist[i].peer_info.peer_addr, mac, sizeof(mac_addr_t));
    int ret = esp_now_add_peer(&peerlist[i].peer_info);
    if(ret != ESP_OK){
        ESP_LOGD(TAG, "Error adding Peer: %i", ret);
        return false;
    }
        
    peerlist[i].active = true;
    peerlist[i].age = 0;
    peer_cnt++;
    return true;
}

static void delete_peer(aodv_peer_t* peer){
    ESP_LOGD(TAG, "Removing Peer");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, peer->peer_info.peer_addr, 6, ESP_LOG_DEBUG);
    peer->active = false;
    peer_cnt--;
    esp_now_del_peer(peer->peer_info.peer_addr);
}

static void process_PREP(peer_msg_t* msg, const mac_addr_t sender, const int rssi, const size_t len){
    if(rssi < CONFIG_AODV_MIN_PEERING_RSSI) //make sure connection is good
        return;

    if(peer_cnt >= (ESP_NOW_MAX_ENCRYPT_PEER_NUM / 2))
        return;

    if(memcmp(msg->src_mac, own_mac, sizeof(mac_addr_t)) != 0)
        return;
    
    add_peer(sender);
}

static void process_PREQ(peer_msg_t* msg, const mac_addr_t sender, const int rssi, const size_t len){
    if(rssi < CONFIG_AODV_MIN_PEERING_RSSI)return; //make sure connection is good

    if(esp_now_is_peer_exist(sender) == false){ //if peer is aready know send rep instantly
        if(peer_cnt >= (ESP_NOW_MAX_ENCRYPT_PEER_NUM))return;
        if(add_peer(sender) == false)return;
    }
    msg->type = PREP;
    esp_now_send(brdcst_addr, (uint8_t*)msg, len);
}

static void process_PPING(const mac_addr_t sender){
    aodv_peer_t* peer = get_peer(sender);
    if(peer == NULL)return;
    peer->age = 0;
}

static void request_peers(){
    ESP_LOGD(TAG, "Requesting new Peers");
    esp_now_send(brdcst_addr, (uint8_t*)&request, sizeof(peer_msg_t));
}

void peering_maintenance(){
    ESP_LOGD(TAG, "Peerdump %i peers:", (int)peer_cnt);
    uint8_t PPING_msg = PPING;
    forward_to_all_peers(&PPING_msg, sizeof(PPING_msg));
    for(int i = 0; i < ESP_NOW_MAX_ENCRYPT_PEER_NUM; i++){
        if(peerlist[i].active == false)continue;
        ESP_LOGD(TAG, "Peer Num: %i Active: %i Age: %i MAC:", (int)i, (int)peerlist[i].active, (int)peerlist[i].age);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, peerlist[i].peer_info.peer_addr, sizeof(mac_addr_t), ESP_LOG_DEBUG);
        peerlist[i].age++;
        if(peerlist[i].age > CONFIG_AODV_PEER_TIMEOUT_AGE)delete_peer(&peerlist[i]);
    }
    if(peer_cnt < (ESP_NOW_MAX_ENCRYPT_PEER_NUM/2))request_peers();
}

void forward_to_all_peers(const uint8_t *buff, size_t len){
    for(int i = 0; i < ESP_NOW_MAX_ENCRYPT_PEER_NUM; i++){
        if(peerlist[i].active == false)continue;
        esp_now_send(peerlist[i].peer_info.peer_addr, buff, len);
    }
}

void reset_peer_table(){
    peer_cnt = 0;
    memset(peerlist, 0, sizeof(peerlist));
}

void process_peering_msg(uint8_t* buff, const mac_addr_t sender, const int rssi, const size_t len){
    if(buff[0] == PPING){
        process_PPING(sender);
        return;
    }

    if(buff[0] == PREQ){
        process_PREQ((peer_msg_t*)buff, sender, rssi, len);
        return;
    }

    if(buff[0] == PREP){
        process_PREP((peer_msg_t*)buff, sender, rssi, len);
        return;
    }
}

void init_peering(esp_now_peer_info_t peer_conf){
    default_peer_conf = peer_conf;
    memcpy(request.src_mac, own_mac, sizeof(mac_addr_t));
    request_peers();
}