#pragma once

#include "aodv.h"
#include <stddef.h>
#include <stdint.h>

void forward_to_all_peers(const uint8_t* buff, size_t len);

void peering_maintenance();

void process_peering_msg(uint8_t* buff, const mac_addr_t sender, const int rssi, const size_t len);

void init_peering(esp_now_peer_info_t peer_conf);

void reset_peer_table();
