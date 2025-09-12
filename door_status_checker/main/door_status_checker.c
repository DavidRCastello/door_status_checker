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

// WIFI constants
#define WIFI_SSID      "MIWIFI_bGYG"
#define WIFI_PASS      "ROUTER_314159"
// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

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

// WIFI variables
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const int MAX_RETRY = 5;  

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
            esp_mqtt_client_publish(mqtt_client, "test", "Hello from ESP32", 0, 1, 0);
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

/* WiFi event handler */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch(event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI("WIFI", "WIFI_EVENT_STA_START -> connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            /* Called when disconnected. Try to reconnect with limited retries. */
            if (s_retry_num < MAX_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI("WIFI", "Retrying to connect to the AP (%d/%d)", s_retry_num, MAX_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGI("WIFI", "Failed to connect after %d attempts.", MAX_RETRY);
            }
            ESP_LOGI("WIFI", "WIFI_EVENT_STA_DISCONNECTED");
            break;

        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI("WIFI", "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

// Initialize WiFi station and attempt to connect
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP network interface (must be first)
    ESP_ERROR_CHECK(esp_netif_init());

    // 2. Initialize NVS — required by WiFi (if not already initialized)
    // nvs_flash_init() should be done in app_main before calling wifi code.
    // (We show it in app_main below.)

    // Create default WiFi station 
    esp_netif_create_default_wifi_sta();

    // Init WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    // Register event handlers for WIFI and IP events
    ESP_ERROR_CHECK( esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL) );
    ESP_ERROR_CHECK( esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL) );

    // Configure the WiFi credentials (station mode)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            /* Optional: set to true to allow connecting to APs without password (open) */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    ESP_LOGI("WIFI", "wifi_init_sta finished.");

    /* Wait for connection or fail event */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdTRUE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(10000)); // optional timeout

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI("WIFI", "Connected to AP: %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI("WIFI", "Failed to connect to SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGI("WIFI", "Connection timed out");
    }
}

static void wifi_start()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Init logging and network stack & start wifi */
    ESP_LOGI("WIFI", "Starting WiFi");
    wifi_init_sta();

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
