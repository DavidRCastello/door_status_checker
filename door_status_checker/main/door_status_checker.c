#include <stdio.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP32 library includes for peripherals
#include "esp_log.h"
#include "driver/gpio.h"
#include "soc/clk_tree_defs.h"

// Defines and constants
#define LED_PIN     32

void init_config(void)
{
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);   
}

void app_main(void)
{
    char *our_task_name = pcTaskGetName(NULL);
    init_config();
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
        ESP_LOGI(our_task_name, "Starting up\n");
        vTaskDelay(1000);
    }
}
