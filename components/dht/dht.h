/**! @file dht.h
 * 
*/
#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DHT_TYPE_11 = 11,
    DHT_TYPE_12 = 12,
    DHT_TYPE_21 = 21,
    DHT_TYPE_22 = 22,
    DHT_TYPE_AM2301 = 21
} dht_sensor_type_t;

typedef struct {
    dht_sensor_type_t sensor_type;
    gpio_num_t gpio_pin;
} dht_config_t;

typedef struct {
    float humidity;
    float temperature;
} dht_data_t;

dht_data_t *dht_get_data();
esp_err_t dht_read();
esp_err_t dht_init();

#ifdef __cplusplus
} /* extern "C" */
#endif