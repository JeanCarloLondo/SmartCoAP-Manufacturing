#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "coap.h"   // tu librería CoAP
#include <dht.h>    // librería DHT (de esp-idf-lib)

// ===== CONFIGURACIÓN =====
#define WIFI_SSID   "TuSSID"       // ⚠️ Cambia por tu red WiFi
#define WIFI_PASS   "TuClave"
#define SERVER_IP   "192.168.1.100" // ⚠️ Cambia a la IP de tu PC (servidor)
#define SERVER_PORT 5683
#define DHT_GPIO    4              // ⚠️ GPIO conectado al pin DATA del DHT22
#define TAG "CoAP_CLIENT"

// ==========================

// Inicialización de WiFi
static void wifi_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Conectando a WiFi...");
    ESP_ERROR_CHECK(esp_wifi_connect());
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

    while (1) {
        // 1. Leer sensor DHT22
        float temp = 0, hum = 0;
        if (dht_read_float_data(DHT_TYPE_DHT22, DHT_GPIO, &hum, &temp) == ESP_OK) {
            ESP_LOGI(TAG, "Sensor DHT22 -> Temp=%.2f°C Hum=%.2f%%", temp, hum);
        } else {
            ESP_LOGE(TAG, "Error leyendo el sensor DHT22");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue; // saltar este ciclo
        }

        // 2. Construir mensaje CoAP
        coap_message_t msg;
        coap_init_message(&msg);
        msg.type = COAP_TYPE_CON;
        msg.code = COAP_CODE_POST;
        msg.message_id = rand() % 65535;

        char payload[64];
        snprintf(payload, sizeof(payload), "temp=%.2f,hum=%.1f", temp, hum);

        msg.payload = (uint8_t*)payload;
        msg.payload_len = strlen(payload);

        uint8_t buffer[256];
        int len = coap_serialize(&msg, buffer, sizeof(buffer));

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
                ESP_LOGI(TAG, "ACK recibido MID=%u, Payload=%.*s",
                         resp.message_id,
                         (int)resp.payload_len,
                         resp.payload ? (char*)resp.payload : "");
                coap_free_message(&resp);
            }
        }

        // Esperar 5 segundos antes de enviar otro
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    close(sock);
    vTaskDelete(NULL);
}

void app_main(void) {
    wifi_init();
    xTaskCreate(coap_client_task, "coap_client_task", 4096, NULL, 5, NULL);
}
