#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "coap.h"   

// ===== CONFIGURACIÓN =====
#define WIFI_SSID   ""       // ⚠️ Cambia por tu red WiFi
#define WIFI_PASS   ""
#define SERVER_IP   ""       // ⚠️ Cambia a la IP de tu PC (servidor)
#define SERVER_PORT 5683
#define TAG "CoAP_CLIENT"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
// ==========================

//Simulated sensor data (puedes reemplazar luego con código de sensor real)
static void read_sensor_data(float *temperature, float *humidity) {
    *temperature = 25.0 + (rand() % 100) / 10.0; // Simulated temperature
    *humidity = 50.0 + (rand() % 100) / 10.0;    // Simulated humidity
}

//event handler for WiFi events
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Reintentando conexión a WiFi...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Conexión a WiFi fallida");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado a WiFi, IP: %s",
                 ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Inicialización de WiFi
static void wifi_init(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

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

    ESP_LOGI(TAG, "Conectando a WiFi...");
    
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conexión a WiFi exitosa");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Fallo al conectar a WiFi");
    }
}

// Tarea principal: enviar datos CoAP
static void coap_client_task(void *pvParameters) {
    int sock;
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Error al crear socket");
        vTaskDelete(NULL);
    }

    // Configurar timeout de recepción
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    ESP_LOGI(TAG, "Socket creado, iniciando envío de datos...");
    while (1) {
        // 1. Leer sensor simulado
        float temp = 0, hum = 0;
        read_sensor_data(&temp, &hum);
        ESP_LOGI(TAG, "Datos del sensor simulado - Temp: %.2f C, Hum: %.1f %%", temp, hum);
       
        // 2. Construir mensaje CoAP
        coap_message_t msg;
        coap_init_message(&msg);
        msg.type = COAP_TYPE_CON;
        msg.code = COAP_CODE_POST;
        msg.message_id = rand() % 65535;
        msg.tkl = 0;

        char payload[64];
        snprintf(payload, sizeof(payload), "temp=%.2f,hum=%.1f", temp, hum);

        msg.payload = (uint8_t*)payload;
        msg.payload_len = strlen(payload);

        // Serializar mensaje CoAP
        uint8_t buffer[256];
        int len = coap_serialize(&msg, buffer, sizeof(buffer));
        if (len <= 0) {
            ESP_LOGE(TAG, "Error serializando mensaje CoAP");
            coap_free_message(&msg);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        // 3. Enviar al servidor
        int err = sendto(sock, buffer, len, 0,
                         (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Error enviando paquete UDP");
        } else {
            ESP_LOGI(TAG, "Mensaje CoAP enviado: %s", payload);
        }

        // 4. Esperar respuesta (ACK)
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        uint8_t rx_buffer[256];
        int len_rx = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                              (struct sockaddr *)&source_addr, &socklen);

        if (len_rx > 0) {
            coap_message_t resp;
            if (coap_parse(rx_buffer, len_rx, &resp) == COAP_OK) {
                // Verificar que sea ACK para nuestro mensaje
                if (resp.type == COAP_TYPE_ACK && resp.message_id == msg.message_id) {
                    ESP_LOGI(TAG, "ACK recibido correctamente (MID=%u)", resp.message_id);
                    if (resp.payload_len > 0) {
                        ESP_LOGI(TAG, "Respuesta del servidor: %.*s", 
                                (int)resp.payload_len, resp.payload);
                    }
                } else {
                    ESP_LOGW(TAG, "Respuesta inesperada: Type=%d, MID=%u (esperaba MID=%u)", 
                            resp.type, resp.message_id, msg.message_id);
                }
                coap_free_message(&resp);
            } else {
                ESP_LOGE(TAG, "Error parseando respuesta CoAP");
            }
        } else if (len_rx == 0) {
            ESP_LOGW(TAG, "No se recibio ACK del servidor");
        } else {
            ESP_LOGE(TAG, "Error en recvfrom");
        }

        // Esperar 10 segundos antes del próximo envío
        ESP_LOGI(TAG, "Esperando 10 segundos para próximo envío...\n");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    close(sock);
    vTaskDelete(NULL);
}

void app_main(void) {
    srand(time(NULL));
    ESP_LOGI(TAG, "Iniciando CoAP Client");

    wifi_init();
    xTaskCreate(coap_client_task, "coap_client_task", 4096, NULL, 5, NULL);
}
