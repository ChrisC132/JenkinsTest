#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "sdkconfig.h"

#include "aodv.h"
#include "aodv_msg_types.h"
#include "aodv_routing.h"
#include "aodv_peering.h"

#define RREQ_BRDCST_SIGN_BUFF_SIZE 10


typedef struct{
    uint8_t type;
    mac_addr_t src_mac; //request initiator
    uint32_t src_seq; //only for rreq
    uint32_t broadcast_id; //only for rreq
    mac_addr_t dst_mac; //destination
    uint32_t dst_seq;
    uint8_t hop_cnt;
} mgmnt_msg_t;

typedef struct route_t{
    mac_addr_t forward_mac;
    mac_addr_t dest_mac;
    uint32_t dst_seq;
    uint8_t age;
    uint8_t hopcount;
    uint32_t pos_in_list;
} route_t;

static const char *TAG = "aodv_routing";

route_t route_list[CONFIG_AODV_ROUTE_TABE_SIZE];
uint32_t route_count = 0;
uint32_t seq_num = 0;

static route_t* find_route(const mac_addr_t dest_mac){
    for(int i = 0; i < route_count; i++){
        if(memcmp(route_list[i].dest_mac, dest_mac, sizeof(mac_addr_t)) == 0){
            route_list[i].age = 0;
            return &route_list[i];
        }
    }
    return NULL;
}

size_t get_route_info(mac_addr_t mac, uint8_t* buff){
    route_t* route = find_route(mac);
    if(route == NULL){
        memset(buff, 0xFF, 6);
        return 6;
    }
    memcpy(buff, &route->dst_seq, 4);
    buff[4] = route->age;
    buff[5] = route->hopcount;
    return 6;
}

static void delete_route(route_t* const delroute){
    route_list[route_count - 1].pos_in_list = delroute->pos_in_list;
    memcpy(delroute, &route_list[route_count - 1], sizeof(route_t)); //move last element in list to element which is to be deleted
    route_count--;
}

static route_t* add_route(const mac_addr_t forward_mac, const mac_addr_t dest_mac, const uint32_t dest_seq, const uint8_t hopcnt){
    if(route_count >= CONFIG_AODV_ROUTE_TABE_SIZE)return NULL;
    route_list[route_count].dst_seq = dest_seq;
    route_list[route_count].hopcount = hopcnt;
    route_list[route_count].age = 0;
    route_list[route_count].pos_in_list = route_count;
    memcpy(route_list[route_count].forward_mac, forward_mac, sizeof(mac_addr_t));
    memcpy(route_list[route_count].dest_mac, dest_mac, sizeof(mac_addr_t));
    route_count++;
    return &route_list[route_count - 1];
}

static void mod_route(route_t* const route, const mac_addr_t forward_mac, const uint32_t dest_seq, const uint8_t hopcnt){
    route->dst_seq = dest_seq;
    route->hopcount = hopcnt;
    route->age = 0;
    memcpy(route_list[route_count].forward_mac, forward_mac, sizeof(mac_addr_t));
}

void request_new_route(const mac_addr_t dest){
    ESP_LOGD(TAG, "Requesting new route");
    mgmnt_msg_t rreq;
    route_t* existing_route = find_route(dest);
    if(existing_route == NULL && route_count >= CONFIG_AODV_ROUTE_TABE_SIZE)return;

    rreq.type = RREQ;
    rreq.src_seq = ++seq_num;
    rreq.broadcast_id = esp_random();
    rreq.hop_cnt = 0;
    rreq.dst_seq = 0;
    if(existing_route != NULL)rreq.dst_seq = existing_route->dst_seq + 1;
    memcpy(rreq.dst_mac, dest, sizeof(mac_addr_t));
    memcpy(rreq.src_mac, own_mac, sizeof(mac_addr_t));
    forward_to_all_peers((uint8_t*)&rreq, sizeof(mgmnt_msg_t));
}

void forward_message(uint8_t* buff, size_t len){
    ESP_LOGD(TAG, "Forwarding MSG");
    forward_msg_header_t* header = (forward_msg_header_t*)buff;
    route_t* route = find_route(header->dst_mac);
    if(route == NULL){
        ESP_LOGD(TAG, "Didnt find Route to:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, header->dst_mac, 6, ESP_LOG_DEBUG);
        return;
    }
    if(esp_now_send(route->forward_mac, buff, len) == ESP_ERR_ESPNOW_NOT_FOUND){
        delete_route(route);
        ESP_LOGD(TAG, "Peer dosent exist anymore:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, header->dst_mac, 6, ESP_LOG_DEBUG);
        return;
    }
    route->age = 0;
}

void routing_maintenance(){
    ESP_LOGD(TAG, "Running Routing Maintnance %i Routes:", (int)route_count);
    for(int i = 0; i < route_count; i++){
        ESP_LOGD(TAG, "Hops: %i Age; %i SeqNum: %i Dest / Nect Hop", (int)route_list[i].hopcount, (int)route_list[i].age, (int)route_list[i].dst_seq);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, route_list[i].dest_mac, sizeof(mac_addr_t), ESP_LOG_DEBUG);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, route_list[i].forward_mac, sizeof(mac_addr_t), ESP_LOG_DEBUG);
        route_list[i].age++;
        if(route_list[i].age > CONFIG_AODV_ROUTE_TIMEOUT_AGE){
            delete_route(&route_list[i]);
        }
    }
}

void reset_route_table(){
    route_count = 0;
    memset(route_list, 0, sizeof(route_list));
}

static bool is_RREQ_known(uint32_t id){
    static uint32_t brd_sig_ring_buff[RREQ_BRDCST_SIGN_BUFF_SIZE]= {0};
    static uint32_t brd_sig_cnt = 0;

    for(int i = 0; i < RREQ_BRDCST_SIGN_BUFF_SIZE; i++){
        if(brd_sig_ring_buff[i] == id){
            return true;
        }
    }
    // add request if unknown
    brd_sig_cnt = (brd_sig_cnt + 1) % RREQ_BRDCST_SIGN_BUFF_SIZE;
    brd_sig_ring_buff[brd_sig_cnt] = id;
    return false;
}

static void process_RREQ(mgmnt_msg_t* msg, const mac_addr_t sender_mac){ //TO-DO: is known peer restarted he hase no enc enabled but this peer still hase so --> send all reeps unencrypted
    ESP_LOGD(TAG, "Processing REQ");
    //ESP_LOG_BUFFER_HEX(TAG, msg->dst_mac, 6);
    if(memcmp(msg->src_mac, own_mac, sizeof(mac_addr_t)) == 0)return; //dont respond to own RREQ

    //check if request is known
    bool rreq_known = is_RREQ_known(msg->broadcast_id);

    route_t* reverse_route = find_route(msg->src_mac);

    if(rreq_known && (reverse_route == NULL || reverse_route->hopcount <= msg->hop_cnt))return; //if request is known check if new request is better

    if(reverse_route != NULL){ //add or mod route depending if it exists
        mod_route(reverse_route, sender_mac, msg->src_seq, msg->hop_cnt);
    }else{
        reverse_route = add_route(sender_mac, msg->src_mac, msg->src_seq, msg->hop_cnt);
        if(reverse_route == NULL)return; //adding route failed
    }
    ESP_LOGD(TAG, "Added reverse route");

    //send rrep if selfe is destination
    if(memcmp(msg->dst_mac, own_mac, sizeof(mac_addr_t)) == 0){
        ESP_LOGD(TAG, "Selfe is dest sending RREP Hops: %i", (int)msg->hop_cnt);
        msg->type = RREP;

        if(msg->dst_seq > seq_num)seq_num = msg->dst_seq; //if own seq should be lower bracuse of reatart or any other reason update to bigger one
        if(rreq_known == false)seq_num++; //only icrement seq_num per brdcst id

        // if(msg->dst_seq >= seq_num)
        //     seq_num = msg->dst_seq; //if newer one is requested or same give that
        // else
        //     seq_num++; //if lower one is requestet increment own and send back

        msg->dst_seq = seq_num;
        msg->hop_cnt = 0;
        esp_now_send(sender_mac, (uint8_t*)msg, sizeof(mgmnt_msg_t));
        return;
    }

    //send rrep if selfe has valide route
    route_t* known_dst_route = find_route(msg->dst_mac);
    if(known_dst_route != NULL && known_dst_route->dst_seq >= msg->dst_seq){
        msg->type = RREP;
        msg->dst_seq = known_dst_route->dst_seq;
        msg->hop_cnt = known_dst_route->hopcount + 1;
        esp_now_send(sender_mac, (uint8_t*)msg, sizeof(mgmnt_msg_t));
        ESP_LOGD(TAG, "Had route to Destination RREP was sent");
        return;
    }else{}//could delete route here because sender determined the route is broken

    msg->hop_cnt++;
    forward_to_all_peers((uint8_t*)msg, sizeof(mgmnt_msg_t));
    ESP_LOGD(TAG, "RREQ forwared to all peers");
}

static void process_RREP(mgmnt_msg_t* msg, const mac_addr_t sender_mac){
    ESP_LOGD(TAG, "Processing REP Hops: %i DestSeq: %i SourceSeq: %i ", (int)msg->hop_cnt, (int)msg->dst_seq, (int)msg->src_seq);
    route_t* forward_route = find_route(msg->dst_mac);

    if(forward_route != NULL)
        ESP_LOGD(TAG, "Known route already found checking if new one is better");

    if(forward_route != NULL && forward_route->dst_seq > msg->dst_seq){
        ESP_LOGD(TAG, "New Route has lower seq num: %i then know Route: %i", (int)msg->dst_seq, (int)forward_route->dst_seq);
        return; //check if rrep is newer or same
    }
    if(forward_route != NULL && forward_route->dst_seq == msg->dst_seq && forward_route->hopcount <= msg->hop_cnt){
        ESP_LOGD(TAG, "New Route has bigger hop num: %i then know Route: %i", (int)msg->hop_cnt, (int)forward_route->hopcount);
        return; //if sequence numbers match check hops and drop if not better
    }

    if(forward_route != NULL){
        mod_route(forward_route, sender_mac, msg->dst_seq, msg->hop_cnt);
    }else{
        forward_route = add_route(sender_mac, msg->dst_mac, msg->dst_seq, msg->hop_cnt);
        if(forward_route == NULL)return; //adding route failed
    }
    ESP_LOGD(TAG, "Added forward peer and route");

    if(memcmp(msg->src_mac, own_mac, sizeof(mac_addr_t)) == 0){
        ESP_LOGD(TAG, "Sucessfully built Route");
        return; //if self is originator of rreq for this rrep --> terminate
    }

    //forward rrep
    route_t* reverse_route = find_route(msg->src_mac);
    if(reverse_route == NULL)return;
    msg->hop_cnt++;
    reverse_route->age = 0;
    esp_now_send(reverse_route->forward_mac, (uint8_t*)msg, sizeof(mgmnt_msg_t));
    ESP_LOGD(TAG, "Forwarded RREP");
}

void process_routing_msg(uint8_t* msg, const mac_addr_t sender){
    if(msg[0] == RREQ){
        process_RREQ((mgmnt_msg_t*)msg, sender);
        return;
    }

    if(msg[0] == RREP){
        process_RREP((mgmnt_msg_t*)msg, sender);
        return;
    }
}