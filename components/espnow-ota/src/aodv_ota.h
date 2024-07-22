#pragma once

#include "aodv.h"
#include <stddef.h>
#include <stdint.h>

typedef char (ota_identifier_t)[16];

typedef enum OTA_MSG_TYPES {
    OTA_MIN = 100, OTA_INIT, OTA_READY, OTA_DATA, OTA_RETRANS,
} ota_msg_type_t;

void process_ota_msg(const mac_addr_t sender, const uint8_t* buff, size_t size, uint8_t service);

void aodv_ota_init(uint8_t service_ID);
