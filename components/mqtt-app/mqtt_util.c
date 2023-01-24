// ESP-IDF Components

// Project Components

// Project Files
#include "mqtt_app.h"
#include "mqtt_util.h"

void mqtt_util_publish(char *topic, char *data, int data_len, int qos, int retain) {
    esp_mqtt_client_publish(mqtt_app_client, topic, data, data_len, qos, retain);
}

void mqtt_util_subscribe(char *topic, int qos) {
    esp_mqtt_client_subscribe(mqtt_app_client, topic, qos);
}

void mqtt_util_unsubscribe(char *topic) {
    esp_mqtt_client_unsubscribe(mqtt_app_client, topic);
}
