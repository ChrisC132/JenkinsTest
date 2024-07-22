#pragma once

#include "nowqtt_common.h"

#define SENSOR_STATE_MAX_SIZE 20

typedef void (*rx_command_handle_t)(const char* state, size_t len);

typedef struct{
    const char* constructor;
    uint8_t has_command_topic;
    char state[SENSOR_STATE_MAX_SIZE];
    rx_command_handle_t handler;
    uint8_t id;
} nowqtt_entity_t;

extern nowqtt_entity_t** nowqtt_entity_list;
extern size_t nowqtt_entity_count;
extern const char* nowqtt_device_config;

void send_log(const char* logmsg);

void update_state(nowqtt_entity_t* entity);

void update_float_state(nowqtt_entity_t* entity, float data);

void update_int_state(nowqtt_entity_t* entity, int data);

void update_char_state(nowqtt_entity_t* entity, const char* data);

void send_influx(const char* influxmsg);

void init_sensors();

void process_rx_data(void *pvParameters);