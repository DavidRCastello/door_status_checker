#include <stdio.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP32 library includes for peripherals
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "soc/clk_tree_defs.h"
#include "esp_timer.h"
#include "freertos/queue.h"

// Defines and constants
#define LED_PIN         32
#define SENSOR_PIN      26

// IR Sensor
#define IR_SENSOR_SECONDS_FOR_STATUS_CHANGE     5
#define IR_SENSOR_US_FOR_STATUS_CHANGE          IR_SENSOR_SECONDS_FOR_STATUS_CHANGE * 1000000

// TASKS time definition
#define IR_SENSOR_TASK_TIME                 100
#define OPEN_BLINKING_LED_TASK_TIME         200
#define CLOSED_BLINKING_LED_TASK_TIME       1000

// Structs definition
typedef enum
{
    CLOSED,
    OPEN
}ir_sensor_status_type;

typedef struct
{
    ir_sensor_status_type current_status;
    int status_change_counter;
    int last_change_timestamp;
    bool computing_new_status;
}ir_sensor_data_t;

// Shared variables
QueueHandle_t shared_queue;

//******************************************************************/
//********************** TASK IR SENSOR ****************************/
//******************************************************************/
int compute_time_diff(int old_timestamp)
{
    int new_time = esp_timer_get_time();
    if(new_time >= old_timestamp)
    {
        return new_time - old_timestamp;
    }
    else
    {
        return INT_MAX - old_timestamp + new_time;
    }
}
void ir_sensor_task(void *pv_parameters)
{
    // '1' is object not detected
    // '0' is object detected
    ir_sensor_data_t ir_sensor_data;
    ir_sensor_data.current_status = gpio_get_level(SENSOR_PIN);
    ir_sensor_data.last_change_timestamp = esp_timer_get_time();
    ir_sensor_data.computing_new_status = false;
    while(1)
    {
        if(ir_sensor_data.computing_new_status == false)
        {
            // Detect change
            uint8_t new_level = gpio_get_level(SENSOR_PIN);
            if(new_level != ir_sensor_data.current_status)
            {
                ir_sensor_data.last_change_timestamp = esp_timer_get_time();
                ir_sensor_data.computing_new_status = true;
                ir_sensor_data.status_change_counter = 0;
                ESP_LOGI("IR_SENSOR_TASK", "Starting computing...");

            }
        }
        else
        {
            if(gpio_get_level(SENSOR_PIN) == OPEN)
            {
                ir_sensor_data.status_change_counter++;
            }
            else
            {
                ir_sensor_data.status_change_counter--;
            }

            if(compute_time_diff(ir_sensor_data.last_change_timestamp) >= IR_SENSOR_US_FOR_STATUS_CHANGE)
            {
                ir_sensor_data.computing_new_status = false;
                ir_sensor_data.last_change_timestamp = esp_timer_get_time();
                if(ir_sensor_data.status_change_counter >= 0)
                {
                    ir_sensor_data.current_status = OPEN;

                }
                else
                {
                    ir_sensor_data.current_status = CLOSED;
                }
                xQueueSend(shared_queue, &ir_sensor_data.current_status, portMAX_DELAY);
                ESP_LOGI("IR_SENSOR_TASK", "Result: %d\n", ir_sensor_data.current_status);

            }
        }

        vTaskDelay(pdMS_TO_TICKS(IR_SENSOR_TASK_TIME));
    }
}

//******************************************************************/
//********************** TASK BLINKING *****************************/
//******************************************************************/
void led_blinking_task(void *pv_parameters)
{
    uint8_t led_status = 0;
    int current_blinking_period = OPEN_BLINKING_LED_TASK_TIME;
    int shared_data = 0;
    while(1)
    {
        if (xQueueReceive(shared_queue, &shared_data, 0)) {
            if(shared_data == OPEN)
            {
                current_blinking_period = OPEN_BLINKING_LED_TASK_TIME;
            }
            else
            {
                current_blinking_period = CLOSED_BLINKING_LED_TASK_TIME;
            }
        }

        // Toggle LED
        led_status++;
        if(led_status > 1)
        {
            led_status = 0;
        }
        gpio_set_level(LED_PIN, led_status);
        vTaskDelay(pdMS_TO_TICKS(current_blinking_period));
    }

}

//******************************************************************/
//********************** MAIN **************************************/
//******************************************************************/
// PRIVATE FUNCTIONS
static void init_config(void)
{
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);  
    gpio_set_direction(SENSOR_PIN, GPIO_MODE_INPUT);  
    
    shared_queue = xQueueCreate(1, sizeof(int));
 
}

void app_main(void)
{
    // INIT GPIO and peripherals
    init_config();
    
    // Start the tasks to run
    xTaskCreate(led_blinking_task, "Blink Task", 2048, NULL, 1, NULL);
    xTaskCreate(ir_sensor_task, "IR Sensor Task", 2048, NULL, 5, NULL);
}
