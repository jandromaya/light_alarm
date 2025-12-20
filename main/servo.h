#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/mcpwm_prelude.h"

// set angle of the servo to param angle
void servo_set_angle(int angle);

// sets up servo control structures (timer, operator, generator, comparator)
void setup_servo();