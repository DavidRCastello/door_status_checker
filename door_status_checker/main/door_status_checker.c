#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

void app_main(void)
{
    char *our_task_name = pcTaskGetName(NULL);
    while(1)
    {
        ESP_LOGI(our_task_name, "Starting up\n");
        vTaskDelay(1000);
    }
}
