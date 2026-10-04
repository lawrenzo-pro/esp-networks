#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_event_base.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "mqtt_client.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAXIMUM_RETRY 5
static const char *TAG = "wifi station";
static const char *MQTT_TAG = "Mqtt client:";
esp_mqtt_client_handle_t mqttClient;
#define WIFI_SSID "Connecticut Highway"
#define WIFI_PASSWORD "My@home--pass15"

EventGroupHandle_t s_wifi_event_group;
static int retry_num = 0;
int isConnected = 0;
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void * event_data){
    if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
        esp_wifi_connect();
    } else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if(retry_num < MAXIMUM_RETRY){
            esp_wifi_connect();
            retry_num++;
            ESP_LOGI(TAG, "retrying...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connecting to AP failed");
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG,"got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        isConnected = 1;
    }
}
void wifi_init_sta(void){
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any;
    esp_event_handler_instance_t got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&event_handler, NULL, &instance_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,&event_handler, NULL,&got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "station init finished");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,pdFALSE,pdFALSE,portMAX_DELAY);
}
static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data){
    ESP_LOGD(MQTT_TAG, "Event dispatched from event loop");
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch((esp_mqtt_event_id_t)event_id){
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTT_TAG, "Connected to broker!");
            msg_id = esp_mqtt_client_subscribe(client, "hello/world", 0);
            ESP_LOGI(MQTT_TAG, "Sent sub successfully, msq_id=%d",msg_id);
        case MQTT_EVENT_DISCONNECTED:
        break;
        case MQTT_EVENT_SUBSCRIBED:
        break;
        case MQTT_EVENT_UNSUBSCRIBED:
        break;
        case MQTT_EVENT_DATA:
        break;
        case MQTT_EVENT_PUBLISHED:
        break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(MQTT_TAG,"MQTT ERROR");
            if(event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT){
                ESP_LOGI(MQTT_TAG,"Error from tls:0x%x",event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(MQTT_TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGI(MQTT_TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                    strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGI(MQTT_TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(MQTT_TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
        break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}
static void mqtt_start(){
    const esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = "mqtt://192.168.9.6",
        .broker.address.port = 1883
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
    mqttClient = client;
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,NULL);
    esp_mqtt_client_start(client);
}
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if( ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "ESP Wifi station");
    wifi_init_sta();

    if(isConnected) mqtt_start();
    while(1){
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
