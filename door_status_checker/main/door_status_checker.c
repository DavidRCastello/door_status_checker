#include <stdio.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP32 library includes for peripherals
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "soc/clk_tree_defs.h"

// Defines and constants
#define LED_PIN         32
#define SENSOR_PIN      26

// TASKS time definition
#define IR_SENSOR_TASK_TIME         100
#define BLINKING_LED_TASK_TIME      500


//******************************************************************/
//********************** TASK IR SENSOR ****************************/
//******************************************************************/
void ir_sensor_task(void *pv_parameters)
{
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(IR_SENSOR_TASK_TIME));
    }
}

//******************************************************************/
//********************** TASK BLINKING *****************************/
//******************************************************************/
void led_blinking_task(void *pv_parameters)
{
    uint8_t led_status = 0;
    while(1)
    {
        // Toggle LED
        led_status++;
        if(led_status > 1)
        {
            led_status = 0;
        }
        gpio_set_level(LED_PIN, led_status);
        vTaskDelay(pdMS_TO_TICKS(BLINKING_LED_TASK_TIME));

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
 
}

void app_main(void)
{
    init_config();
    
    xTaskCreate(led_blinking_task, "Blink Task", 2048, NULL, 1, NULL);
    xTaskCreate(ir_sensor_task, "IR Sensor Task", 2048, NULL, 5, NULL);

    // while(1)
    // {   

    //     // Read the IR sensor value
    //     uint8_t level = gpio_get_level(SENSOR_PIN);
    //     ESP_LOGI(our_task_name, "LEVEL: %d\n", level);
        
    // }
}
