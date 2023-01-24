// ESP-IDF Components
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Project Components
#include "dht.h"

// Project Files
#include "temp_controller.h"

#define DEBUG_TEMP_CONTROLLER 1
#define DHT_PIN_MASKED (1ULL<<CONFIG_TC_OUT_GPIO_PIN)
static gpio_num_t gpio_pin = CONFIG_TC_OUT_GPIO_PIN;
static gpio_config_t gpio_tc_config = {
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .pin_bit_mask = DHT_PIN_MASKED,
};

static const char *TAG = "temp-controller";

static void temp_controller_task() {
    bool cooling = false;
    for ( ;; ) {
        float temperature = dht_get_data()->temperature;
        ESP_LOGD(TAG, "Temperature: %f, Setpoint: %f", temperature, temp_controller_setpoint);

        if(!cooling && temperature >= temp_controller_setpoint) {
            gpio_set_level(gpio_pin, 1);
            cooling = true;
            ESP_LOGD(TAG, "Enabling cooling functionality");
        }

        if(cooling && temperature < (temp_controller_setpoint - CONFIG_TC_DEADBAND)) {
            gpio_set_level(gpio_pin, 0);
            cooling = false;
            ESP_LOGD(TAG, "Disabling cooling functionality");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void temp_controller_init(bool enable) {
#if DEBUG_TEMP_CONTROLLER
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#else
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif

    gpio_config(&gpio_tc_config);
    temp_controller_setpoint = (float)CONFIG_TC_DEFAULT_SETPOINT;
    if (enable)
        temp_controller_enable();
}

static TaskHandle_t tcTask;
void temp_controller_enable() {
    xTaskCreate(temp_controller_task, "temp_controller_task", 2048, NULL, 1, &tcTask);
}

void temp_controller_disable() {
    if(tcTask)
        vTaskDelete(tcTask);
}