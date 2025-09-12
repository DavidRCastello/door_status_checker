#include <stdio.h>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "freertos/event_groups.h"

// ESP32 library includes for peripherals
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "soc/clk_tree_defs.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "mqtt_client.h"
#include <string.h>
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"

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

// WiFi variables
const char *wifi_ssid = "MIWIFI_bGYG";
const char *wifi_pass = "ROUTER_314159";
int wifi_retry_num = 5;

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
static esp_mqtt_client_handle_t mqtt_client = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT", "Connected to broker");
            // Publish once after connecting
            // esp_mqtt_client_publish(mqtt_client, "test", "Hello from ESP32", 0, 1, 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW("MQTT", "Disconnected from broker");
            break;
        default:
            break;
    }
}

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
                ESP_LOGI("IR_SENSOR_TASK", "Start computing...");
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

                if(ir_sensor_data.current_status == OPEN)
                {
                    ESP_LOGI("IR_SENSOR_TASK", "Result: OPEN\n");
                    esp_mqtt_client_publish(mqtt_client, "test", "OPEN", 0, 1, 0);
                }
                else
                {
                    ESP_LOGI("IR_SENSOR_TASK", "Result: CLOSED\n");
                    esp_mqtt_client_publish(mqtt_client, "test", "CLOSED", 0, 1, 0);
                }
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

static void mqtt_client_start()
{
    // 1. Configure the client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://192.168.1.159:1883",  // <-- Change to your RPi's IP
    };

    // 2. Init client
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    // 3. Register event handler
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    // 4. Start client
    esp_mqtt_client_start(mqtt_client);
}
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,void *event_data){
    if(event_id == WIFI_EVENT_STA_START)
    {
        printf("WIFI CONNECTING....\n");
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
        printf("WiFi CONNECTED\n");
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        printf("WiFi lost connection\n");
        if(wifi_retry_num < 5)
        {
            esp_wifi_connect();
            wifi_retry_num++;
            printf("Retrying to Connect...\n");
        }
    }
    else if (event_id == IP_EVENT_STA_GOT_IP)
    {
        printf("Wifi got IP...\n\n");
    }
}
static void wifi_start()
{
    // Init NVS flash
    nvs_flash_init();

    // Wi-Fi Configuration Phase
    esp_netif_init();
    esp_event_loop_create_default();     // event loop                    s1.2
    esp_netif_create_default_wifi_sta(); // WiFi station                      s1.3
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation); //     
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = "",
            .password = "",
            
           }
    
        };
    strcpy((char*)wifi_configuration.sta.ssid, wifi_ssid);
    strcpy((char*)wifi_configuration.sta.password, wifi_pass);    
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    // Wi-Fi Start Phase
    esp_wifi_start();
    esp_wifi_set_mode(WIFI_MODE_STA);
    // Wi-Fi Connect Phase
    esp_wifi_connect();
    printf( "wifi_init_softap finished. SSID:%s  password:%s", wifi_ssid, wifi_pass);
}

void app_main(void)
{
    // INIT GPIO and peripherals
    init_config();
    
    // Connect to WIFI network
    wifi_start();

    // Start MQTT
    mqtt_client_start();

    // Start the tasks to run
    xTaskCreate(led_blinking_task, "Blink Task", 2048, NULL, 1, NULL);
    xTaskCreate(ir_sensor_task, "IR Sensor Task", 2048, NULL, 5, NULL);
}
