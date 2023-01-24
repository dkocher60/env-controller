/**! @file temp_controller.h
 * 
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

float temp_controller_setpoint;
void temp_controller_init(bool enable);
void temp_controller_enable();
void temp_controller_disable();

#ifdef __cplusplus
} /* extern "C" */
#endif