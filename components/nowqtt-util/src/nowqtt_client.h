#pragma once

#include "nowqtt_client_common.h"

void nowqtt_init(const char* device_config, nowqtt_entity_t** entitys, size_t len, uint8_t* sec_key, uint32_t heartbeat_sek, bool retransmitt, mac_addr_t bridge_addr);