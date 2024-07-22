#pragma once
#include "aodv.h"
#include <stdint.h>

typedef enum MSG_TYPES {
    PREQ, PREP, MAX_BRDCST, //Messages using unencrypted broudcast
    PPING, MAX_PEERING,
    RREQ, RREP, MAX_ROUTING, TRACEREQ, TRACEREP, MAX_TRACE, NET_RESET, MAX_RESET, MAX_INTERNAL = 20,
    //All Messages bigger MAX_ROUTING begin with forward_msg_header_t and are forwarded automatically to their destination
} msg_type_t;

enum DATA_MSG_TYPE {DATA = 1, DATA_QOS, DATA_ACK, MAX_DATA = 10};

#define MAX_SERVICE_COUNT (UINT8_MAX - MAX_INTERNAL) / MAX_DATA

typedef struct{
    uint8_t type;
    mac_addr_t dst_mac;
    mac_addr_t src_mac;
} forward_msg_header_t;