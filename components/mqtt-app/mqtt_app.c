// ESP-IDF Components
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"

// Project Components

// Project Files
#include "mqtt_app.h"

#define DEBUG_MQTT 1

static const char *TAG = "mqtt-app";
static int connection_attempts = 0; // Connection attempt count. Reset on successful connection
static esp_mqtt_client_config_t client_cfg;
static mqtt_app_connected_callback_handler_t connected_callback_handler = NULL;
static mqtt_app_disconnected_callback_handler_t disconnected_callback_handler = NULL;
static mqtt_app_data_callback_handler_t data_callback_handler = NULL;

#ifdef CONFIG_MQTT_USE_SSL
extern const uint8_t ssl_cert_pem_start[]   asm("_binary_mqtt_broker_pem_start");
extern const uint8_t ssl_cert_pem_end[]   asm("_binary_mqtt_broker_pem_end");
#endif

static void set_lwt_config() {
    client_cfg.lwt_topic = mqtt_app_lwt_info.topic;
    client_cfg.lwt_msg = mqtt_app_lwt_info.msg;
    client_cfg.lwt_msg_len = mqtt_app_lwt_info.msg_len;
    ESP_LOGD(TAG, "LWT Topic: %s", client_cfg.lwt_topic);
    esp_mqtt_set_config(mqtt_app_client, &client_cfg);
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            connection_attempts = 0;
            mqtt_app_connected = true;
            if(connected_callback_handler)
                connected_callback_handler();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_app_connected = false;
            disconnected_callback_handler();
            if (connection_attempts < CONFIG_MQTT_MAX_ATTEMPTS) {
                vTaskDelay(pdMS_TO_TICKS(CONFIG_MQTT_RECONNECT_DELAY * 1000));
                esp_mqtt_client_reconnect(event->client);
            }
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "MQTT_EVENT_SUBSCRIBED");
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "MQTT_EVENT_UNSUBSCRIBED");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT_EVENT_DATA. TOPIC=%.*s", event->topic_len, event->topic);
            if(data_callback_handler)
                data_callback_handler(event->topic, event->topic_len, event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGW(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGW(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGW(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                        strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGW(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGD(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            connection_attempts++;
            set_lwt_config();
            break;
        default:
            ESP_LOGW(TAG, "Unknown MQTT event dispatched. Event ID:%d", event->event_id);
            break;
    }
}

/**
 * @brief Init MQTT Application
*/
void mqtt_app_init() {
#if DEBUG_MQTT
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#else
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif
    mqtt_app_connected = false;
    client_cfg = (esp_mqtt_client_config_t) {};
    client_cfg.uri = CONFIG_MQTT_BROKER_URI;
    client_cfg.disable_auto_reconnect = true;
    client_cfg.keepalive = CONFIG_MQTT_KEEPALIVE_PERIOD;
    client_cfg.disable_keepalive = !CONFIG_MQTT_AUTO_KEEPALIVE_ENABLED;
    client_cfg.lwt_qos = 1;
    client_cfg.lwt_retain = true;
#ifdef CONFIG_MQTT_USE_SSL
    client_cfg.cert_pem = (const char *)ssl_cert_pem_start;
#endif
#ifdef CONFIG_MQTT_AUTH_ENABLED
    client_cfg.username = CONFIG_MQTT_USERNAME;
    client_cfg.password = CONFIG_MQTT_PASSWORD;
#endif

    ESP_LOGD(TAG, "Free heap size: %d. Min: %d", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    mqtt_app_client = esp_mqtt_client_init(&client_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(mqtt_app_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_app_client);
}

void mqtt_app_stop() {
    if(mqtt_app_connected)
        esp_mqtt_client_disconnect(mqtt_app_client);

    esp_mqtt_client_destroy(mqtt_app_client);
}

void mqtt_app_connected_callback_register(mqtt_app_connected_callback_handler_t callback_handler) {
    connected_callback_handler = callback_handler;
}

void mqtt_app_disconnected_callback_register(mqtt_app_disconnected_callback_handler_t callback_handler) {
    disconnected_callback_handler = callback_handler;
}

void mqtt_app_data_callback_register(mqtt_app_data_callback_handler_t callback_handler) {
    data_callback_handler = callback_handler;
}
