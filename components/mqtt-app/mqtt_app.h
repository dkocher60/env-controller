/**! @file mqtt.h
 * 
*/
#pragma once

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char* topic;
    char* msg;
    ssize_t msg_len;
} mqtt_app_lwt_info_t;

typedef void (*mqtt_app_connected_callback_handler_t)(void);
typedef void (*mqtt_app_disconnected_callback_handler_t)(void);
typedef void (*mqtt_app_data_callback_handler_t)(char *topic, int topic_len, char *data, int data_len);

esp_mqtt_client_handle_t mqtt_app_client;
mqtt_app_lwt_info_t mqtt_app_lwt_info;
bool mqtt_app_connected;
void mqtt_app_init();
void mqtt_app_stop();
void mqtt_app_connected_callback_register(mqtt_app_connected_callback_handler_t callback_handler);
void mqtt_app_disconnected_callback_register(mqtt_app_disconnected_callback_handler_t callback_handler);
void mqtt_app_data_callback_register(mqtt_app_data_callback_handler_t callback_handler);

#ifdef __cplusplus
} /* extern "C" */
#endif