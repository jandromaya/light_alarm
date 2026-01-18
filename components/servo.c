#include "servo.h"
#include "esp_log.h"

static const char *TAG = "servo";

// Please consult the datasheet of your servo before changing the following parameters
#define SERVO_MIN_PULSEWIDTH_US 500  // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH_US 2500  // Maximum pulse width in microsecond
#define SERVO_MIN_DEGREE        -90   // Minimum angle
#define SERVO_MAX_DEGREE        90    // Maximum angle

#define SERVO_PULSE_GPIO             0        // GPIO connects to the PWM signal line
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000  // 1MHz, 1us per tick
#define SERVO_TIMEBASE_PERIOD        20000    // 20000 ticks, 20ms

mcpwm_timer_handle_t timer = NULL;
mcpwm_oper_handle_t oper = NULL;
mcpwm_cmpr_handle_t comparator = NULL;
mcpwm_gen_handle_t generator = NULL;

// ---------------- PRIVATE FUNCTIONS -------------
static inline int32_t angle_to_compare(int angle) {
    ESP_LOGI(TAG, "CHANGING ANGLE TO %d", angle);
    return (angle - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
}


mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SERVO_TIMEBASE_RESOLUTION_HZ,
        .period_ticks = SERVO_TIMEBASE_PERIOD,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
};

mcpwm_operator_config_t operator_config = {
        .group_id = 0,
};

mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
};

mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = SERVO_PULSE_GPIO,  
};


static void servo_create_timer() {
    

    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));
}

static void servo_create_oper() {
   

    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));
}

static void servo_connect_timer_operator() {
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));
}

static void servo_create_comparator() {
    

    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));
}

static void servo_create_generator() {

    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));
}

static void servo_set_action() {
    ESP_LOGI(TAG, "Set generator action on timer and compare event");
    // go high on counter empty
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
                                                              MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    // go low on compare threshold
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
                                                                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW)));
}

static void servo_timer_enable_and_start() {
    ESP_LOGI(TAG, "Enable and start timer");
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
}

//  --------------- PUBLIC FUNCTIONS --------------

void setup_servo() {
    // creating the basic components
    servo_create_timer();
    servo_create_oper();
    servo_connect_timer_operator();

    servo_create_comparator();
    servo_create_generator();

    // setting intial compare value, so servo goes to center
    servo_set_angle(0);

    // setting the action on timer an comparator events
    servo_set_action();
    servo_timer_enable_and_start();
}

void servo_set_angle(int angle) {
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, angle_to_compare(angle)));
}



