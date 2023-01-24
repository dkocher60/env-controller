// ESP-IDF Components
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Project Components
// None

// Project Files
#include "dht.h"

#define DEBUG_DHT 0

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#define PORT_ENTER_CRITICAL() portENTER_CRITICAL(&mux)
#define PORT_EXIT_CRITICAL() portEXIT_CRITICAL(&mux)

// Helper macro used by fetch_data() function. Will release the timing lock and log an error
//  if read result doesn't return OK.
#define CHECK_LOGE(x, msg, ...) do { \
        esp_err_t __; \
        if ((__ = x) != ESP_OK) { \
            PORT_EXIT_CRITICAL(); \
            ESP_LOGE(TAG, msg, ## __VA_ARGS__); \
            return __; \
        } \
    } while (0)

#define DHT_DATA_BITS 40
#define DHT_DATA_BYTES (DHT_DATA_BITS / 8)
#define DHT_PIN_MASKED (1ULL<<CONFIG_DHT_GPIO)
#define MIN_INTERVAL 2000

static const char *TAG = "dht";
static uint32_t max_cycles;
static uint32_t last_read_time;
static uint32_t pulse_cycles[DHT_DATA_BITS*2];
static uint8_t data[DHT_DATA_BYTES];
static dht_config_t dht_config = {
    .gpio_pin = CONFIG_DHT_GPIO,
#ifdef CONFIG_DHT_SENSOR_TYPE_11
    .sensor_type = DHT_TYPE_11,
#elif CONFIG_DHT_SENSOR_TYPE_12
    .sensor_type = DHT_TYPE_12,
#elif CONFIG_DHT_SENSOR_TYPE_21
    .sensor_type = DHT_TYPE_21,
#elif CONFIG_DHT_SENSOR_TYPE_22
    .sensor_type = DHT_TYPE_22,
#elif CONFIG_DHT_SENSOR_TYPE_AM2301
    .sensor_type = DHT_TYPE_AM2301,
#endif
};
static dht_data_t dht_data = {
    .humidity = 0.0,
    .temperature = -99.0,
};

static gpio_config_t gpio_config_input_pu = {
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .pin_bit_mask = DHT_PIN_MASKED,
};
static gpio_config_t gpio_config_output = {
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .pin_bit_mask = DHT_PIN_MASKED,
};

// Function prototypes
static void dht_read_task();
static esp_err_t fetch_data();
static esp_err_t wait_while_level(int level, uint32_t *duration);
static void convert_data();
static void convert_temperature(float *temperature);
static void convert_humidity(float *humidity);

/*  DHT Sensor Data Transmission Breakdown
 *  Note:
 *  A suitable pull-up resistor should be connected to the selected GPIO line
 *
 *  __           ______          _______                              ___________________________
 *    \    A    /      \   C    /       \   DHT duration_data_low    /                           \
 *     \_______/   B    \______/    D    \__________________________/   DHT duration_data_high    \__
 *
 *
 *  Initializing communications with the DHT requires four 'phases' as follows:
 *
 *  Phase A - ESP pulls signal low for at least 18000 us
 *  Phase B - ESP allows signal to float back up and waits 20-40us for DHT to pull it low
 *  Phase C - DHT pulls signal low for ~80us
 *  Phase D - DHT lets signal float back up for ~80us
 *
 *  After this, the DHT transmits its first bit by holding the signal low for 50us
 *  and then letting it float back high for a period of time that depends on the data bit.
 *  duration_data_high is shorter than 50us for a logic '0' and longer than 50us for logic '1'.
 *
 *  There are a total of 40 data bits transmitted sequentially. These bits are read into a byte array
 *  of length 5.  The first and third bytes are humidity (%) and temperature (C), respectively.  Bytes 2 and 4
 *  are zero-filled and the fifth is a checksum such that:
 *
 *  byte_5 == (byte_1 + byte_2 + byte_3 + byte_4) & 0xFF
 *
 */

dht_data_t *dht_get_data() { return &dht_data; }

esp_err_t dht_read() {
    // Check if sensor was read less than two seconds ago
    uint32_t current_time = esp_timer_get_time();
    if ((current_time - last_read_time) < MIN_INTERVAL) {
        return ESP_ERR_INVALID_STATE;
    }
    last_read_time = current_time;

    // Send start signal.  See DHT datasheet for full signal diagram:
    //   http://www.adafruit.com/datasheets/Digital%20humidity%20and%20temperature%20sensor%20AM2302.pdf

    // Phase A: First set data line low for a period according to sensor type
    gpio_config(&gpio_config_output);
    gpio_set_level(dht_config.gpio_pin, 0);

    switch (dht_config.sensor_type) {
        case DHT_TYPE_22:
        case DHT_TYPE_21:
            ets_delay_us(1100); // data sheet says "at least 1ms"
            break;
        case DHT_TYPE_11:
        default:
            ets_delay_us(20); // data sheet says at least 18ms, 20ms just to be safe
            break;
    }

    // Phase B: End the start signal by setting data line high for 20-40 microseconds.
    gpio_config(&gpio_config_input_pu);
    ets_delay_us(40);

    // Phase C/D: Receive sensor Tx start message and collect data
    // These operations are timing critical so disable task interrupts
    esp_err_t fetch_result = fetch_data();
    if (fetch_result != ESP_OK)
        return fetch_result;
    
    // Reset 40 bits of received data to zero.
    data[0] = data[1] = data[2] = data[3] = data[4] = 0;
    
    // Inspect pulses and determine which ones are 0 (high state cycle count < low
    // state cycle count), or 1 (high state cycle count > low state cycle count).
    for (int i = 0; i < DHT_DATA_BITS; ++i) {
        uint32_t low_cycles = pulse_cycles[2 * i];
        uint32_t high_cycles = pulse_cycles[2 * i + 1];
        data[i / 8] <<= 1;

        // Now compare the low and high cycle times to see if the bit is a 0 or 1.
        if (high_cycles > low_cycles) {
            // High cycles are greater than 50us low cycle count, must be a 1.
            data[i / 8] |= 1;
        }
        // Else high cycles are less than (or equal to, a weird case) the 50us low
        // cycle count so this must be a zero.  Nothing needs to be changed in the
        // stored data.
    }

    ESP_LOGD(TAG, "Received from DHT:");
    ESP_LOGD(TAG, "[%02X], [%02X], [%02X], [%02X] | [%02X] =? [%02X]", 
                    data[0], data[1], data[2], data[3], data[4], 
                    (data[0] + data[1] + data[2] + data[3]) & 0xFF);

    // Check we read 40 bits and that the checksum matches.
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        ESP_LOGE(TAG, "Checksum failed, invalid data received from sensor");
        return ESP_ERR_INVALID_CRC;
    }

    convert_data();
    return ESP_OK;
}

esp_err_t dht_init() {
#if DEBUG_DHT
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#else
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif
    max_cycles = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000; // 1000 us (1 ms) in cycles
    last_read_time = esp_timer_get_time() - MIN_INTERVAL;
    gpio_config(&gpio_config_input_pu);
    xTaskCreate(dht_read_task, "dht_read_task", 2048, NULL, 2, NULL);
    ESP_LOGD(TAG, "DHT Sensor Initialized. Max Cycles: %d", max_cycles);

    return ESP_OK;
}

static void dht_read_task() {
    for ( ;; ) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DHT_READ_RATE));
        esp_err_t read_result = dht_read();
        if (read_result == ESP_OK)
            ESP_LOGD(TAG, "Sensor data: humidity=%f, temperature=%f", dht_data.humidity, dht_data.temperature);
    }
}

static esp_err_t fetch_data() {
    PORT_ENTER_CRITICAL();
    // Start reading the data line to get the value from the DHT sensor.
    // Phase C: Wait ~80 microseconds for signal change low->high
    CHECK_LOGE(wait_while_level(0, NULL), "Timeout waiting for start signal low->high transition.");
    // Phase D: Wait ~80 microseconds for signal chanlge high->low
    CHECK_LOGE(wait_while_level(1, NULL), "Timeout waiting for start signal high->low transition.");

    // Now read the 40 bits sent by the sensor.  Each bit is sent as a 50
    // microsecond low pulse followed by a variable length high pulse.  If the
    // high pulse is ~28 microseconds then it's a 0 and if it's ~70 microseconds
    // then it's a 1.  We measure the cycle count of the initial 50us low pulse
    // and use that to compare to the cycle count of the high pulse to determine
    // if the bit is a 0 (high state cycle count < low state cycle count), or a
    // 1 (high state cycle count > low state cycle count). Note that for speed
    // all the pulses are read into a array and then examined in a later step.
    for (int i = 0; i < DHT_DATA_BITS * 2; i += 2) {
        CHECK_LOGE(wait_while_level(0, &pulse_cycles[i]), "Timeout waiting for data pulse low->high transition.");
        CHECK_LOGE(wait_while_level(1, &pulse_cycles[i+1]), "Timeout witing for data pulse high->low transition.");
    }

    PORT_EXIT_CRITICAL();
    return ESP_OK;
}

static esp_err_t wait_while_level(int level, uint32_t *duration) {
    uint32_t count = 0;
    while (gpio_get_level(dht_config.gpio_pin) == level) {
        if (count++ >= max_cycles) {
            return ESP_ERR_TIMEOUT; // Exceeded timeout, fail.
        }
    }
    if(duration)
        *duration = count;
    return ESP_OK;
}

static void convert_data() {
    convert_temperature(&dht_data.temperature);
    convert_humidity(&dht_data.humidity);
}

static void convert_temperature(float *temperature) {
    float f = -99.0;
    switch (dht_config.sensor_type) {
    case DHT_TYPE_11:
      f = data[2];
      if (data[3] & 0x80) {
        f = -1 - f;
      }
      f += (data[3] & 0x0f) * 0.1;
      break;
    case DHT_TYPE_12:
      f = data[2];
      f += (data[3] & 0x0f) * 0.1;
      if (data[2] & 0x80) {
        f *= -1;
      }
      break;
    case DHT_TYPE_22:
    case DHT_TYPE_21:
      f = ((uint8_t)(data[2] & 0x7F)) << 8 | data[3];
      f *= 0.1;
      if (data[2] & 0x80) {
        f *= -1;
      }
      break;
    }

  *temperature = f;
}

static void convert_humidity(float *humidity) {
    float f = 0.0;
    switch (dht_config.sensor_type) {
    case DHT_TYPE_11:
    case DHT_TYPE_12:
      f = data[0] + data[1] * 0.1;
      break;
    case DHT_TYPE_22:
    case DHT_TYPE_21:
      f = ((uint8_t)data[0]) << 8 | data[1];
      f *= 0.1;
      break;
    }
  *humidity = f;
}
