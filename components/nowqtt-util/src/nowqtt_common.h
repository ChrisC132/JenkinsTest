#pragma once
#include <stdint.h>
#include "aodv.h"

typedef struct{
    uint8_t command;
    uint8_t sensor_id;
} msg_header_t;

typedef enum COMMANDS {RESET, HANDSHAKE, CONFIG, STATE, COMMAND, INFLUX, LOG, ACK} commands_t;

void nowqtt_comm_init(bool retransmitt, mac_addr_t bridge_mac, uint8_t* key);

void put_in_send_queue(commands_t type, size_t len, const uint8_t* data , uint8_t id);

void get_from_receive_queue(size_t* payload_size, uint8_t* payload);
