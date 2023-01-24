/**! @file mqtt_util.h
 * 
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_util_publish(char *topic, char *data, int data_len, int qos, int retain);
void mqtt_util_subscribe(char *topic, int qos);
void mqtt_util_unsubscribe(char *topic);

#ifdef __cplusplus
} /* extern "C" */
#endif