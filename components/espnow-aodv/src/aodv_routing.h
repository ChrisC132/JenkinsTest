#pragma once

#include "aodv.h"

void forward_message(uint8_t* buff, size_t len);

void request_new_route(const mac_addr_t dest);

void routing_maintenance();

void process_routing_msg(uint8_t* msg, const mac_addr_t sender);

void reset_route_table();

size_t get_route_info(mac_addr_t mac, uint8_t* buff);