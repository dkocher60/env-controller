/**! @file sparkplug.h
 * 
*/
#pragma once

#include "tahu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_TAHU_METRIC_TYPE_UNKNOWN = METRIC_DATA_TYPE_UNKNOWN,
    ESP_TAHU_METRIC_TYPE_INT8 = METRIC_DATA_TYPE_INT8,
    ESP_TAHU_METRIC_TYPE_INT16 = METRIC_DATA_TYPE_INT16,
    ESP_TAHU_METRIC_TYPE_INT32 = METRIC_DATA_TYPE_INT32,
    ESP_TAHU_METRIC_TYPE_INT64 = METRIC_DATA_TYPE_INT64,
    ESP_TAHU_METRIC_TYPE_UINT8 = METRIC_DATA_TYPE_UINT8,
    ESP_TAHU_METRIC_TYPE_UINT16 = METRIC_DATA_TYPE_UINT16,
    ESP_TAHU_METRIC_TYPE_UINT32 = METRIC_DATA_TYPE_UINT32,
    ESP_TAHU_METRIC_TYPE_UINT64 = METRIC_DATA_TYPE_UINT64,
    ESP_TAHU_METRIC_TYPE_FLOAT = METRIC_DATA_TYPE_FLOAT,
    ESP_TAHU_METRIC_TYPE_DOUBLE = METRIC_DATA_TYPE_DOUBLE,
    ESP_TAHU_METRIC_TYPE_BOOLEAN = METRIC_DATA_TYPE_BOOLEAN,
    ESP_TAHU_METRIC_TYPE_STRING = METRIC_DATA_TYPE_STRING,
    ESP_TAHU_METRIC_TYPE_DATETIME = METRIC_DATA_TYPE_DATETIME,
    ESP_TAHU_METRIC_TYPE_TEXT = METRIC_DATA_TYPE_TEXT,
    ESP_TAHU_METRIC_TYPE_UUID = METRIC_DATA_TYPE_UUID,
    ESP_TAHU_METRIC_TYPE_DATASET = METRIC_DATA_TYPE_DATASET,
    ESP_TAHU_METRIC_TYPE_BYTES = METRIC_DATA_TYPE_BYTES,
    ESP_TAHU_METRIC_TYPE_FILE = METRIC_DATA_TYPE_FILE,
    ESP_TAHU_METRIC_TYPE_TEMPLATE =  METRIC_DATA_TYPE_TEMPLATE
} esp_tahu_metric_type_t;

typedef void (*esp_tahu_metric_change_callback_t)(void *new_value);

typedef struct {
    char *device_id;
    char *metric_name;
    bool has_alias;
    uint64_t alias;
    esp_tahu_metric_type_t data_type;
    bool is_historical;
    bool is_transient;
    bool readback_value_change;
    esp_tahu_metric_change_callback_t on_change_callback;
    union {
        uint32_t int_value;
        uint64_t long_value;
        float float_value;
        double double_value;
        bool boolean_value;
        char *string_value;
    } value;
} esp_tahu_metric_t;

typedef void (*esp_tahu_next_server_callback_handler_t)(void);
typedef void (*esp_tahu_rebirth_callback_handler_t)(void);
typedef void (*esp_tahu_reboot_callback_handler_t)(void);

void esp_tahu_configure(char *sp_node_id);
void esp_tahu_init_node();
void esp_tahu_init_device(char *device_id, esp_tahu_metric_t *metrics, size_t metrics_count);
void esp_tahu_deinit_device(char *device_id);
esp_err_t esp_tahu_create_metric(char *device_id, esp_tahu_metric_t *metric_buffer, char *metric_name, esp_tahu_metric_type_t data_type, esp_tahu_metric_change_callback_t on_change_callback);
void *esp_tahu_get_metric_data(esp_tahu_metric_t *metric);
esp_err_t esp_tahu_set_metric_data(esp_tahu_metric_t *metric, bool zero_value, void *new_value);
void esp_tahu_publish_ddata(esp_tahu_metric_t *metrics, size_t metrics_count);
esp_err_t esp_tahu_data_received(char *payload_buffer, size_t payload_length, esp_tahu_metric_t *metrics, size_t metrics_count);
void esp_tahu_get_lwt_data(char *lwt_topic_buffer, char *lwt_msg_buffer, size_t lwt_msg_buffer_len, ssize_t *out_msg_len);
void esp_tahu_register_next_server_callback_handler(esp_tahu_next_server_callback_handler_t callback_handler);
void esp_tahu_register_rebirth_callback_handler(esp_tahu_rebirth_callback_handler_t callback_handler);
void esp_tahu_register_reboot_callback_handler(esp_tahu_reboot_callback_handler_t callback_handler);

#ifdef __cplusplus
} /* extern "C" */
#endif