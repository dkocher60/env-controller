// ESP-IDF Components
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// Project Components
#include "dht.h"
#include "mqtt_app.h"
#include "esp_tahu.h"
#include "wifi.h"

#if CONFIG_APP_IMPL_TEMP_CONTROLLER
#include "temp_controller.h"
#endif

// Project Files
// None

#define DEBUG_APP 0

typedef enum {
    SP_METRIC_DHT_TEMPERATURE,
    SP_METRIC_DHT_HUMIDITY,
    SP_METRIC_TC_SETPOINT,
    SP_METRIC_COUNT
} sp_metrics_t;

// Function Prototypes
static char *get_wifi_mac_id();
static void loop_panic_msg();
static void shutdown_handler();
static bool string_starts_with(char *prefix, char *str);
static void mqtt_update_lwt();
static void mqtt_connect_handler();
static void mqtt_disconnect_handler();
static void mqtt_data_handler(char *topic, int topic_len, char *data, int data_len);
static void tahu_data_publish_task();
static void tahu_initialize();
static void tahu_build_metrics();
static void tahu_ncmd_reboot();
static void tahu_ncmd_rebirth();
static void tc_setpoint_change();

static const char *TAG = "app-main";
static bool wifi_connected;
static bool sp_initialized = false;
static const int SP_METRIC_COUNT_DHT = 2;
static const int SP_METRIC_COUNT_TC = 1;
static esp_tahu_metric_t *sp_metrics;

static char *get_wifi_mac_id()
{
    uint8_t mac[6];
    char *id_string = calloc(1, 32);
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(id_string, "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return id_string;
}

static void loop_panic_msg() {
    ESP_LOGW(TAG, "ESP Previously Exited on Panic Abort. Will not execute application.");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void shutdown_handler() {
    mqtt_app_stop();

    if (wifi_connected)
        wifi_disconnect();
}

static bool string_starts_with(char *prefix, char *str) {
    return strncmp(prefix, str, strlen(prefix)) == 0;
}

static void mqtt_update_lwt() {
    mqtt_app_lwt_info_t lwt_info = {
            .topic = (char *)malloc(sizeof(char) * 64),
            .msg = (char *)malloc(sizeof(char) * 256),
            .msg_len = 0,
    };
    esp_tahu_get_lwt_data(lwt_info.topic, lwt_info.msg, 256, &lwt_info.msg_len);
    mqtt_app_lwt_info = lwt_info;
}

static void mqtt_connect_handler() {
    tahu_initialize();
}

static void mqtt_disconnect_handler() {
    sp_initialized = false;
    mqtt_update_lwt();
}

static void mqtt_data_handler(char *topic, int topic_len, char *data, int data_len) {
    if (string_starts_with("spBv1.0", topic)) {
        esp_err_t result = esp_tahu_data_received(data, data_len, sp_metrics, SP_METRIC_COUNT);
        if(result != ESP_OK)
            ESP_LOGW(TAG, "Error interpreting command from topic: %.*s", topic_len, topic);
    }
}

static void tahu_data_publish_task() {
    for ( ;; ) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        if(mqtt_app_connected) {
            if(!sp_initialized)
                continue;

            esp_tahu_set_metric_data(sp_metrics + SP_METRIC_DHT_TEMPERATURE, false, (void *) &dht_get_data()->temperature);
            esp_tahu_set_metric_data(sp_metrics + SP_METRIC_DHT_HUMIDITY, false, (void *) &dht_get_data()->humidity);

            esp_tahu_publish_ddata(sp_metrics, SP_METRIC_COUNT_DHT);
        }
    }
}

static void tahu_initialize() {
    esp_tahu_init_node();
    esp_tahu_init_device(CONFIG_APP_SPARKPLUG_DEVICE_ID_DHT, sp_metrics, SP_METRIC_COUNT_DHT);

    if(CONFIG_APP_IMPL_TEMP_CONTROLLER)
        esp_tahu_init_device(CONFIG_APP_SPARKPLUG_DEVICE_ID_TEMP_CONTROLLER, sp_metrics+SP_METRIC_COUNT_DHT, 
                        SP_METRIC_COUNT_TC);
    sp_initialized = true;
}

static void tahu_build_metrics() {
    int active_metric_count = 0;
    if(!CONFIG_APP_IMPL_TEMP_CONTROLLER)
        active_metric_count = SP_METRIC_COUNT_DHT;
    else
        active_metric_count = SP_METRIC_COUNT;

    sp_metrics = (esp_tahu_metric_t *) malloc(sizeof(esp_tahu_metric_t) * active_metric_count);
    for(int i = 0; i < SP_METRIC_COUNT_DHT; i++) {
        esp_tahu_metric_t new_metric = { 0 };
        switch((sp_metrics_t)i) {
            case SP_METRIC_DHT_TEMPERATURE:
                esp_tahu_create_metric(CONFIG_APP_SPARKPLUG_DEVICE_ID_DHT, &new_metric, "temperature", ESP_TAHU_METRIC_TYPE_FLOAT, NULL);
                break;
            case SP_METRIC_DHT_HUMIDITY:
                esp_tahu_create_metric(CONFIG_APP_SPARKPLUG_DEVICE_ID_DHT, &new_metric, "humidity", ESP_TAHU_METRIC_TYPE_FLOAT, NULL);
                break;
            default:
                ESP_LOGW(TAG, "Undefined metric. Index: %d", i);
                break;
        }
        memcpy(sp_metrics+i, &new_metric, sizeof(new_metric));
    }
    if(CONFIG_APP_IMPL_TEMP_CONTROLLER) {
        for(int i = SP_METRIC_COUNT_DHT; i < SP_METRIC_COUNT; i++) {
            esp_tahu_metric_t new_metric = { 0 };
            switch((sp_metrics_t)i) {
                case SP_METRIC_TC_SETPOINT:
                    esp_tahu_create_metric(CONFIG_APP_SPARKPLUG_DEVICE_ID_TEMP_CONTROLLER, &new_metric, "setpoint", ESP_TAHU_METRIC_TYPE_FLOAT, tc_setpoint_change);
                    break;
                default:
                    ESP_LOGW(TAG, "Undefined metric. Index: %d", i);
                    break;
            }
            memcpy(sp_metrics+i, &new_metric, sizeof(new_metric));
        }
    }
}

static void tahu_ncmd_reboot() {
    esp_restart();
}

static void tahu_ncmd_rebirth() {
    sp_initialized = false;
    mqtt_update_lwt();
    tahu_initialize();

}

static void tc_setpoint_change(float *new_value) {
    ESP_LOGI(TAG, "Received new setpoint value: %f", *new_value);
    temp_controller_setpoint = *new_value;
}

/**
 * @brief Initialize and start all app functionality
*/
void app_main() {
#if DEBUG_APP
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#else
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif

    if(esp_reset_reason() == ESP_RST_PANIC)
        loop_panic_msg();

    ESP_LOGI(TAG, "Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_register_shutdown_handler(shutdown_handler);

    dht_init();
#if CONFIG_APP_IMPL_TEMP_CONTROLLER
    temp_controller_init(true);
#endif

    // Connect WiFi interface
    wifi_connected = wifi_connect() == ESP_OK;

    if (wifi_connected)
    {
        // Configure sparkplug and get LWT info
        mqtt_app_lwt_info_t lwt_info = {
            .topic = (char *)malloc(sizeof(char) * 64),
            .msg = (char *)malloc(sizeof(char) * 256),
            .msg_len = 0,
        };
        esp_tahu_configure(get_wifi_mac_id());
        esp_tahu_get_lwt_data(lwt_info.topic, lwt_info.msg, 256, &lwt_info.msg_len);
        esp_tahu_register_rebirth_callback_handler(tahu_ncmd_rebirth);
        esp_tahu_register_reboot_callback_handler(tahu_ncmd_reboot);
        tahu_build_metrics();

        // Connect MQTT client
        mqtt_app_lwt_info = (mqtt_app_lwt_info_t) {};
        memcpy(&mqtt_app_lwt_info, &lwt_info, sizeof(lwt_info));
        mqtt_app_connected_callback_register(mqtt_connect_handler);
        mqtt_app_disconnected_callback_register(mqtt_disconnect_handler);
        mqtt_app_data_callback_register(mqtt_data_handler);
        mqtt_app_init();
    }

    xTaskCreate(tahu_data_publish_task, "tahu_data_publish_task", 2500, NULL, 1, NULL);
}