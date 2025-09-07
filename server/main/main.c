// main.c
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG = "sensor-hub";

/* Credentials */
#define WIFI_SSID       ""
#define WIFI_PASS       ""
#define MQQT_BROKER_URL ""

#define UART_NUM            UART_NUM_1
#define UART_RX_PIN         16
#define UART_TX_PIN         17
#define UART_DE_PIN         4
#define UART_BAUD_RATE      115200
#define UART_BUF_SIZE       (2048)

/* FreeRTOS queues */
static QueueHandle_t uart_queue;
static QueueHandle_t publish_queue;

/* MQTT client handle */
static esp_mqtt_client_handle_t mqtt_client = NULL;

static char *dup_str(const char *src) {
    size_t n = strlen(src) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, src, n);
    return p;
}

/* UART initialization */
static void uart_setup(void) {
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN,
                                 UART_DE_PIN, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
}

/* Read lines from UART and push to uart_queue */
static void uart_read_task(void *arg) {
    uint8_t *buf = malloc(UART_BUF_SIZE);
    size_t idx = 0;
    while (1) {
        int len = uart_read_bytes(UART_NUM, buf + idx, 1, pdMS_TO_TICKS(2000));
        if (len > 0) {
            if (buf[idx] == '\n' || idx >= UART_BUF_SIZE - 2) {
                buf[idx+1] = '\0';
                ESP_LOGI(TAG, "UART RAW: %s", buf);  // log everything
                char *line = dup_str((char*)buf);
                if (line) {
                    if (xQueueSend(uart_queue, &line, pdMS_TO_TICKS(100)) != pdPASS) {
                        ESP_LOGW(TAG, "uart_queue full, dropping");
                        free(line);
                    }
                }
                idx = 0;
            } else {
                idx += len;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(buf);
    vTaskDelete(NULL);
}

/* Parser task: validate JSON and push to publish_queue */
static void parser_task(void *arg) {
    for (;;) {
        char *raw = NULL;
        if (xQueueReceive(uart_queue, &raw, portMAX_DELAY) == pdPASS) {
            cJSON *json = cJSON_Parse(raw);
            if (json) {
                char *out = cJSON_PrintUnformatted(json);
                cJSON_Delete(json);
                if (out) {
                    if (xQueueSend(publish_queue, &out, pdMS_TO_TICKS(100)) != pdPASS) {
                        ESP_LOGW(TAG, "publish_queue full, dropping");
                        free(out);
                    }
                }
            } else {
                ESP_LOGW(TAG, "invalid json, dropping: %s", raw);
            }
            free(raw);
        }
    }
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT incoming data: topic=%.*s data=%.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);
            break;
        default:
            ESP_LOGI(TAG, "MQTT event id=%d", event->event_id);
            break;
    }
}

/* Create & start MQTT client */
static void mqtt_app_start(void) {
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQQT_BROKER_URL,
    };

    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client,
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   NULL);
    esp_mqtt_client_start(mqtt_client);
}

/* MQTT publisher task */
static void mqtt_publish_task(void *arg) {
    for (;;) {
        char *msg = NULL;
        if (xQueueReceive(publish_queue, &msg, portMAX_DELAY) == pdPASS) {
            cJSON *j = cJSON_Parse(msg);
            char topic[128] = {0};
            if (j) {
                cJSON *id = cJSON_GetObjectItem(j, "id");
                if (cJSON_IsNumber(id)) {
                    snprintf(topic, sizeof(topic), "sensors/bluepill/%d", id->valueint);
                } else {
                    snprintf(topic, sizeof(topic), "sensors/bluepill/unknown");
                }
                cJSON_Delete(j);
            } else {
                snprintf(topic, sizeof(topic), "sensors/bluepill/raw");
            }

            int msg_id = esp_mqtt_client_publish(mqtt_client, topic, msg, 0, 1, 0);
            ESP_LOGI(TAG, "MQTT publish id=%d topic=%s payload=%s", msg_id, topic, msg);
            free(msg);
        }
    }
}

/* WiFi event handler */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        mqtt_app_start();
    }
}

/* WiFi init STA */
static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi init finished. SSID:%s", WIFI_SSID);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    uart_setup();

    uart_queue = xQueueCreate(16, sizeof(char*));
    publish_queue = xQueueCreate(16, sizeof(char*));
    xTaskCreate(uart_read_task, "uart_read", 4096, NULL, 10, NULL);
    xTaskCreate(parser_task, "parser", 4096, NULL, 8, NULL);
    xTaskCreate(mqtt_publish_task, "mqtt_pub", 4096, NULL, 6, NULL);

    wifi_init_sta();   // connect WiFi first, MQTT starts after IP
}
