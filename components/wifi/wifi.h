/**! @file wifi.h
 * 
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_connect();
esp_err_t wifi_disconnect();

#ifdef __cplusplus
} /* extern "C" */
#endif