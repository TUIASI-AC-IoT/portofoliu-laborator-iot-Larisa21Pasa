/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_tls.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define CONFIG_ESP_WIFI_SSID      "lab-iot"
#define CONFIG_ESP_WIFI_PASS      "IoT-IoT-IoT"
#define CONFIG_ESP_MAXIMUM_RETRY  5
#define CONFIG_LOCAL_PORT         10001

//TODO: Modificati adresa IP de mai jos pentru a coincide cu cea a PC-ul pe care rulati scriptul python
#define CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL "https://192.168.245.210:5000/firmware.bin"
#define VERSION_URL "https://192.168.245.210:5000/versioning"
#define LAST_VERSION_CODE "0"
#define MAX_HTTP_OUTPUT_BUFFER 2048

#define GPIO_OUTPUT_IO 4
#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_IO)
#define GPIO_INPUT_IO 2
#define GPIO_INPUT_PIN_SEL (1ULL<<GPIO_INPUT_IO)

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_event_start_ota;
#define BIT_BTN_PRESSED    BIT0


#define TAG "sensor"
#define SENSOR_FILE "/spiffs/sensor.json"
#define SERVER_URL "https://192.168.245.210:5000/sensor/1"  // Flask URL


static const char *TAG = "simple_ota_example";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");
char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};

static int s_retry_num = 0;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASS);
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    return false;
}

static void version_task()
{
        xEventGroupWaitBits(s_event_start_ota, BIT_BTN_PRESSED, pdTRUE, pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Starting VERSION TASK");
        esp_http_client_config_t config = {
            .url = VERSION_URL,
            .cert_pem = (char *)server_cert_pem_start,
            .cert_len = 1422,
            .event_handler = _http_event_handler,
            .user_data = local_response_buffer,
            .keep_alive_enable = true,
            .use_global_ca_store = true,
            .skip_cert_common_name_check = true
        };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    printf("CONTENTUL REZULTAT \"%s\"", config.event_handler);

   while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}

static void ota_task(void *pvParameters)
{
    xEventGroupWaitBits(s_event_start_ota, BIT_BTN_PRESSED, pdTRUE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Starting OTA example task");
    esp_http_client_config_t config = {
        .url = CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL,
        .cert_pem = (char *)server_cert_pem_start,
        .cert_len = 1422,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
        .use_global_ca_store = true,
        .skip_cert_common_name_check = true
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    ESP_ERROR_CHECK(esp_tls_init_global_ca_store());
    ESP_ERROR_CHECK(esp_tls_set_global_ca_store((unsigned char*)server_cert_pem_start, server_cert_pem_end - server_cert_pem_start));

    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void button_task(void * pvParameter)
{
    uint8_t u8Count = 5;
    int val = 1;

    while(1)
    {
        if (gpio_get_level(GPIO_INPUT_IO) != val)
            u8Count--;
        else
            u8Count = 5;

        if(!u8Count) {
            val = gpio_get_level(GPIO_INPUT_IO);

            if(!gpio_get_level(GPIO_INPUT_IO)){
                ESP_LOGI(TAG, "Button pressed");
                xEventGroupSetBits(s_event_start_ota, BIT_BTN_PRESSED);
                val = gpio_get_level(GPIO_INPUT_IO);
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void gpio_init()
{
    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
}
/*Laborator 12*/

static void init_fs() {
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
}

static void write_sensor_file(float value, float target) {
    FILE* f = fopen(SENSOR_FILE, "w");
    if (f) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "id", "sensor1");
        cJSON_AddNumberToObject(root, "value", value);
        cJSON_AddNumberToObject(root, "target", target);
        char *out = cJSON_PrintUnformatted(root);
        fwrite(out, 1, strlen(out), f);
        fclose(f);
        cJSON_free(out);
        cJSON_Delete(root);
    } else {
        ESP_LOGE(TAG, "Cannot open sensor file for writing");
    }
}

static float read_target_from_file() {
    FILE* f = fopen(SENSOR_FILE, "r");
    if (!f) return 22.0;
    char buffer[256];
    fread(buffer, 1, sizeof(buffer), f);
    fclose(f);
    cJSON *root = cJSON_Parse(buffer);
    float target = 22.0;
    if (root) {
        cJSON *t = cJSON_GetObjectItem(root, "target");
        if (cJSON_IsNumber(t)) target = t->valuedouble;
        cJSON_Delete(root);
    }
    return target;
}

static void update_sensor_from_server() {
    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .cert_pem = NULL,
        .event_handler = NULL,
        .skip_cert_common_name_check = true
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            char buffer[256];
            int len = esp_http_client_read_response(client, buffer, sizeof(buffer));
            buffer[len] = 0;
            ESP_LOGI(TAG, "Received: %s", buffer);
            // extrage doar target
            cJSON *json = cJSON_Parse(buffer);
            if (json) {
                cJSON *t = cJSON_GetObjectItem(json, "target");
                cJSON *v = cJSON_GetObjectItem(json, "value");
                float target = cJSON_IsNumber(t) ? t->valuedouble : 22.0;
                float value = cJSON_IsNumber(v) ? v->valuedouble : 21.0;
                write_sensor_file(value, target);
                cJSON_Delete(json);
            }
        }
    }
    esp_http_client_cleanup(client);
}

void sensor_task(void *pvParam) {
    float val = 21.0;
    while (1) {
        float target = read_target_from_file();
        val += (target - val) * 0.1f; // simulează apropierea de target
        write_sensor_file(val, target);
        update_sensor_from_server();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}


void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_init();

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    bool connected = wifi_init_sta();

    if (connected) {
        s_event_start_ota = xEventGroupCreate();
        init_fs();
        xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);

        xTaskCreate(version_task, "version_task", 4096, NULL, 5, NULL);
        // if version_old==version_new
        // nothing
        // else do otatask
        xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL);
        xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
    }
}