// ESP-IDF Components
#include "esp_log.h"
#include "esp_system.h"

// Project Components
#include "mqtt_util.h"

// Project Files
#include "esp_tahu.h"
#include "tahu.h"

#define DEBUG_SPARKPLUG 0

typedef enum {
    MSG_TYPE_NBIRTH,
    MSG_TYPE_NDEATH,
    MSG_TYPE_DBIRTH,
    MSG_TYPE_DDEATH,
    MSG_TYPE_NDATA,
    MSG_TYPE_DDATA,
    MSG_TYPE_NCMD,
    MSG_TYPE_DCMD,
    MSG_TYPE_STATE,
    MSG_TYPE_CNT
} msg_type_t;

typedef enum {
    NCMD_NEXT_SERVER,
    NCMD_REBIRTH,
    NCMD_REBOOT,
    NCMD_COUNT
} ncmd_t;


static const char *TAG = "esp-tahu";
static esp_tahu_rebirth_callback_handler_t next_server_callback;
static esp_tahu_rebirth_callback_handler_t rebirth_callback;
static esp_tahu_rebirth_callback_handler_t reboot_callback;
static int bdSeq = 0;
static char *node_id;
static char *topic_root;
static uint64_t alias_num = NCMD_COUNT;
static const char *msg_type_topic[MSG_TYPE_CNT] = {
    "NBIRTH",
    "NDEATH",
    "DBIRTH",
    "DDEATH",
    "NDATA",
    "DDATA",
    "NCMD",
    "DCMD",
    "STATE"
};

static void get_topic(char *topic_buffer, msg_type_t msg_type, char *device_id) {
    if (!device_id)
        sprintf(topic_buffer, "%s/%s/%s", topic_root, msg_type_topic[msg_type], node_id);
    else
        sprintf(topic_buffer, "%s/%s/%s/%s", topic_root, msg_type_topic[msg_type], node_id, device_id);
}

static uint64_t get_alias() {
    return ++alias_num;
}

static ssize_t get_nbirth_payload(uint8_t *payload_buffer, size_t buffer_length) {
    ESP_LOGD(TAG, "Creating NBIRTH payload.");
    org_eclipse_tahu_protobuf_Payload payload;
    reset_sparkplug_sequence();
    get_next_payload(&payload);

    add_simple_metric(&payload, "bdSeq", false, 0, METRIC_DATA_TYPE_INT32, false, false, &bdSeq, sizeof(bdSeq));
    
    // Add node control metrics
    ESP_LOGD(TAG, "Adding node control metric: 'Node Control/Next Server");
    uint64_t next_server_alias = (uint64_t)NCMD_NEXT_SERVER;
    bool next_server_value = false;
    add_simple_metric(&payload, "Node Control/Next Server", true, next_server_alias, ESP_TAHU_METRIC_TYPE_BOOLEAN, false, false, &next_server_value, sizeof(next_server_value));
    
    ESP_LOGD(TAG, "Adding node control metric: 'Node Control/Rebirth'");
    uint64_t rebirth_alias = (uint64_t)NCMD_REBIRTH;
    bool rebirth_value = false;
    add_simple_metric(&payload, "Node Control/Rebirth", true, rebirth_alias, ESP_TAHU_METRIC_TYPE_BOOLEAN, false, false, &rebirth_value, sizeof(rebirth_value));
    
    ESP_LOGD(TAG, "Adding node control metric: 'Node Control/Reboot'");
    uint64_t reboot_alias = (uint64_t)NCMD_REBOOT;
    bool reboot_value = false;
    add_simple_metric(&payload, "Node Control/Reboot", true, reboot_alias, ESP_TAHU_METRIC_TYPE_BOOLEAN, false, false, &reboot_value, sizeof(reboot_value));
    
    ssize_t msg_len = encode_payload(payload_buffer, buffer_length, &payload);
    bdSeq++;

#if DEBUG_SPARKPLUG
    ESP_LOGD(TAG, "NBIRTH payload constructed.");
    print_payload(&payload);
#endif

    free_payload(&payload);
    return msg_len;
}

static ssize_t get_ndeath_payload(uint8_t *payload_buffer, size_t buffer_length) {
    ESP_LOGD(TAG, "Creating NDEATH payload.");
    org_eclipse_tahu_protobuf_Payload payload = org_eclipse_tahu_protobuf_Payload_init_zero;

    add_simple_metric(&payload, "bdSeq", false, 0, ESP_TAHU_METRIC_TYPE_INT32, false, false, &bdSeq, sizeof(bdSeq));
    ssize_t msg_len = encode_payload(payload_buffer, buffer_length, &payload);

#if DEBUG_SPARKPLUG
    ESP_LOGD(TAG, "NDEATH payload constructed.");
    print_payload(&payload);
#endif
    
    free_payload(&payload);
    return msg_len;
}

static ssize_t get_device_payload(uint8_t *payload_buffer, size_t buffer_length, char *device_id, esp_tahu_metric_t *metrics, size_t metrics_count) {
    ESP_LOGD(TAG, "Creating Device payload.");
    org_eclipse_tahu_protobuf_Payload payload;
    get_next_payload(&payload);

    for(int i = 0; i < metrics_count; i++) {
        ESP_LOGD(TAG, "Creating metric: '%s'", metrics[i].metric_name);
        void *metric_value = esp_tahu_get_metric_data(&metrics[i]);
        add_simple_metric(&payload, metrics[i].metric_name, metrics[i].has_alias, metrics[i].alias, metrics[i].data_type, 
                            metrics[i].is_historical, metrics[i].is_transient, metric_value, sizeof(*metric_value));
    }

#if DEBUG_SPARKPLUG
    ESP_LOGD(TAG, "Device payload constructed.");
    print_payload(&payload);
#endif

    // Encode the payload into a binary format so it can be published in the MQTT message.
    // The binary_buffer must be large enough to hold the contents of the binary payload
    ssize_t msg_len = encode_payload(payload_buffer, buffer_length, &payload);
    free_payload(&payload);
    return msg_len;
}

static ssize_t get_ddeath_payload(uint8_t *payload_buffer, size_t buffer_length) {
    ESP_LOGD(TAG, "Creating DDEATH payload.");
    org_eclipse_tahu_protobuf_Payload payload;
    get_next_payload(&payload);

    ssize_t msg_len = encode_payload(payload_buffer, buffer_length, &payload);
    free_payload(&payload);
    return msg_len;
}

void esp_tahu_configure(char *sp_node_id) {
#if DEBUG_SPARKPLUG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#else
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif

    node_id = sp_node_id;
    topic_root = (char *)calloc(1, 32);
    sprintf(topic_root, "spBv1.0/%s", CONFIG_SPARKPLUG_GROUP_ID);
}

void esp_tahu_init_node() {
    char *topic = (char *)malloc(sizeof(char) * 64);
    get_topic(topic, MSG_TYPE_NCMD, NULL);
    mqtt_util_subscribe(topic, 0);
    
    uint8_t *nbirth_buffer = (uint8_t *) malloc(sizeof(uint8_t) * 1024);
    ssize_t nbirth_len = get_nbirth_payload(nbirth_buffer, 1024);
    
    get_topic(topic, MSG_TYPE_NBIRTH, NULL);
    mqtt_util_publish(topic, (char *)nbirth_buffer, nbirth_len, 0, 0);
    free(topic);
    free(nbirth_buffer);
}

void esp_tahu_init_device(char *device_id, esp_tahu_metric_t *metrics, size_t metrics_count) {
    char *topic = (char *)malloc(sizeof(char) * 64);
    get_topic(topic, MSG_TYPE_DCMD, device_id);
    mqtt_util_subscribe(topic, 0);
    
    uint8_t *dbirth_buffer = (uint8_t *) malloc(sizeof(uint8_t) * 1024);
    ssize_t dbirth_len = get_device_payload(dbirth_buffer, 1024, device_id, metrics, metrics_count);
    
    get_topic(topic, MSG_TYPE_DBIRTH, device_id);
    mqtt_util_publish(topic, (char *)dbirth_buffer, dbirth_len, 0, 0);
    free(topic);
    free(dbirth_buffer);
}

void esp_tahu_deinit_device(char *device_id) {
    uint8_t *ddeath_buffer = (uint8_t *) malloc(sizeof(uint8_t) * 1024);
    ssize_t ddeath_len = get_ddeath_payload(ddeath_buffer, 1024);
    char *topic = (char *)malloc(sizeof(char) * 64);
    get_topic(topic, MSG_TYPE_DDEATH, device_id);

    mqtt_util_publish(topic, (char *)ddeath_buffer, ddeath_len, 0, 0);
    free(ddeath_buffer);

    get_topic(topic, MSG_TYPE_DCMD, device_id);
    mqtt_util_unsubscribe(topic);

    free(topic);
}

void esp_tahu_publish_ddata(esp_tahu_metric_t *metrics, size_t metrics_count) {
    uint8_t *ddata_buffer = (uint8_t *) malloc(sizeof(uint8_t) * 1024);
    ssize_t ddata_len = get_device_payload(ddata_buffer, 1024, metrics->device_id, metrics, metrics_count);
    char *topic = (char *)malloc(sizeof(char) * 64);
    get_topic(topic, MSG_TYPE_DDATA, metrics->device_id);

    mqtt_util_publish(topic, (char *)ddata_buffer, ddata_len, 0, 0);
    free(ddata_buffer);
    free(topic);
}

esp_err_t esp_tahu_data_received(char *payload_buffer, size_t payload_length, esp_tahu_metric_t *metrics, size_t metrics_count) {
    // Decode the payload
    org_eclipse_tahu_protobuf_Payload inbound_payload = org_eclipse_tahu_protobuf_Payload_init_zero;
    if (decode_payload(&inbound_payload, (uint8_t *)payload_buffer, payload_length) == -1) {
        ESP_LOGW(TAG, "Failed to decode payload.");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Get the number of metrics in the payload and iterate over them handling them as needed
    bool known_command = false;
    for (int i = 0; i < inbound_payload.metrics_count; i++) {
        ESP_LOGD(TAG, "Received metric alias: %lld.", inbound_payload.metrics[i].alias);
        switch (inbound_payload.metrics[i].alias) {
            case NCMD_NEXT_SERVER:  // Next Server
                if(next_server_callback)
                    next_server_callback();
                known_command = true;
                break;
            case NCMD_REBIRTH:  // Rebirth
                if(rebirth_callback)
                    rebirth_callback();
                known_command = true;
                break;
            case NCMD_REBOOT:  // Reboot
                if(reboot_callback)
                    reboot_callback();
                known_command = true;
                break;
            default:
                for(int j = 0; j < metrics_count; j++) {
                    if ((metrics+j)->alias == inbound_payload.metrics[i].alias) {
                        ESP_LOGD(TAG, "Metric found.");
                        esp_tahu_set_metric_data(metrics+j, false, &inbound_payload.metrics[i].value);
                        if ((metrics+j)->readback_value_change)
                            esp_tahu_publish_ddata(metrics+j, 1);
                        known_command = true;
                    }
                }
                break;
            }
    }
    free_payload(&inbound_payload);

    if(known_command)
        return ESP_OK;
    else
        return ESP_ERR_INVALID_ARG;
}

void esp_tahu_get_lwt_data(char *lwt_topic_buffer, char *lwt_msg_buffer, size_t lwt_msg_buffer_len, ssize_t *out_msg_len) {
    get_topic(lwt_topic_buffer, MSG_TYPE_NDEATH, NULL);
    *out_msg_len = get_ndeath_payload((uint8_t *) lwt_msg_buffer, lwt_msg_buffer_len);
}

esp_err_t esp_tahu_create_metric(char *device_id, esp_tahu_metric_t *metric_buffer, char *metric_name, esp_tahu_metric_type_t data_type, esp_tahu_metric_change_callback_t on_change_callback) {
    esp_tahu_metric_t metric = {
        .device_id = device_id,
        .metric_name = metric_name,
        .readback_value_change = true,
        .on_change_callback = on_change_callback,
        .has_alias = true,
        .alias = get_alias(),
        .data_type = data_type,
        .is_historical = false,
        .is_transient = false,
    };

    esp_err_t result = esp_tahu_set_metric_data(metric_buffer, true, NULL);
    *metric_buffer = metric;
    return result;
}

void *esp_tahu_get_metric_data(esp_tahu_metric_t *metric) {
    switch(metric->data_type) {
        case ESP_TAHU_METRIC_TYPE_INT8:
        case ESP_TAHU_METRIC_TYPE_INT16:
        case ESP_TAHU_METRIC_TYPE_INT32:
        case ESP_TAHU_METRIC_TYPE_UINT8:
        case ESP_TAHU_METRIC_TYPE_UINT16:
        case ESP_TAHU_METRIC_TYPE_UINT32:
            return &metric->value.int_value;
        case ESP_TAHU_METRIC_TYPE_INT64:
        case ESP_TAHU_METRIC_TYPE_UINT64:
            return &metric->value.long_value;
            break;
        case ESP_TAHU_METRIC_TYPE_FLOAT:
            return &metric->value.float_value;
        case ESP_TAHU_METRIC_TYPE_DOUBLE:
            return &metric->value.double_value;
        case ESP_TAHU_METRIC_TYPE_BOOLEAN:
            return &metric->value.boolean_value;
        case ESP_TAHU_METRIC_TYPE_STRING:
            return &metric->value.string_value;
        case ESP_TAHU_METRIC_TYPE_DATETIME:
        case ESP_TAHU_METRIC_TYPE_TEXT:
        case ESP_TAHU_METRIC_TYPE_UUID:
        case ESP_TAHU_METRIC_TYPE_DATASET:
        case ESP_TAHU_METRIC_TYPE_BYTES:
        case ESP_TAHU_METRIC_TYPE_FILE:
        case ESP_TAHU_METRIC_TYPE_TEMPLATE:
        default:
            ESP_LOGW(TAG, "Unable to retrieve metric value for metric `%s` of type %d", metric->metric_name, metric->data_type);
            return NULL;
    }
}

esp_err_t esp_tahu_set_metric_data(esp_tahu_metric_t *metric, bool zero_value, void *new_value) {
    switch(metric->data_type) {
        case ESP_TAHU_METRIC_TYPE_INT8:
        case ESP_TAHU_METRIC_TYPE_INT16:
        case ESP_TAHU_METRIC_TYPE_INT32:
        case ESP_TAHU_METRIC_TYPE_UINT8:
        case ESP_TAHU_METRIC_TYPE_UINT16:
        case ESP_TAHU_METRIC_TYPE_UINT32:
            if (zero_value)
                metric->value.int_value = 0;
            else if (new_value)
                metric->value.int_value = *(uint32_t *) new_value;
            else
                return ESP_ERR_INVALID_ARG;
            break;
        case ESP_TAHU_METRIC_TYPE_INT64:
        case ESP_TAHU_METRIC_TYPE_UINT64:
            if (zero_value)
                metric->value.long_value = 0;
            else if (new_value)
                metric->value.long_value = *(uint64_t *) new_value;
            else
                return ESP_ERR_INVALID_ARG;
            break;
        case ESP_TAHU_METRIC_TYPE_FLOAT:
            if (zero_value)
                metric->value.float_value = 0;
            else if(new_value)
                metric->value.float_value = *(float *) new_value;
            else
                return ESP_ERR_INVALID_ARG;
            break;
        case ESP_TAHU_METRIC_TYPE_DOUBLE:
            if (zero_value)
                metric->value.double_value = 0;
            else if(new_value)
                metric->value.double_value = *(double *) new_value;
            else
                return ESP_ERR_INVALID_ARG;
            break;
        case ESP_TAHU_METRIC_TYPE_BOOLEAN:
            if (zero_value)
                metric->value.boolean_value = false;
            else if (new_value)
                metric->value.boolean_value = *(bool *) new_value;
            else
                return ESP_ERR_INVALID_ARG;
            break;
        case ESP_TAHU_METRIC_TYPE_STRING:
            if (zero_value)
                metric->value.string_value = strdup("");
            else if (new_value)
                metric->value.string_value = (char *) new_value;
            else
                return ESP_ERR_INVALID_ARG;
            break;
        case ESP_TAHU_METRIC_TYPE_DATETIME:
        case ESP_TAHU_METRIC_TYPE_TEXT:
        case ESP_TAHU_METRIC_TYPE_UUID:
        case ESP_TAHU_METRIC_TYPE_DATASET:
        case ESP_TAHU_METRIC_TYPE_BYTES:
        case ESP_TAHU_METRIC_TYPE_FILE:
        case ESP_TAHU_METRIC_TYPE_TEMPLATE:
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }

    if(metric->on_change_callback)
        metric->on_change_callback(new_value);
    
    return ESP_OK;
}

void esp_tahu_register_next_server_callback_handler(esp_tahu_next_server_callback_handler_t callback_handler) {
    next_server_callback = callback_handler;
}

void esp_tahu_register_rebirth_callback_handler(esp_tahu_rebirth_callback_handler_t callback_handler) {
    rebirth_callback = callback_handler;
}

void esp_tahu_register_reboot_callback_handler(esp_tahu_reboot_callback_handler_t callback_handler) {
    reboot_callback = callback_handler;
}
