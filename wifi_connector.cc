#include "wifi_connector.hh"

#include <cstring>

#include <freertos/FreeRTOS.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

namespace wifi_connect {

  #define TAG "wifi_connect::Connector"

  ////////////////////////////////
  // Public methods

  Connector& Connector::getInstance() {
    static Connector instance;
    return instance;
  }

  bool Connector::connect(wifi_auth_mode_t auth_mode, const char* ssid, const char* password) {
    ESP_ERROR_CHECK(
      esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &Connector::wifiEventHandler,
        this,
        &any_id_handler
      )
    );

    ESP_ERROR_CHECK(
      esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &Connector::gotIPEventHandler,
        this,
        &got_ip_handler
      )
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    ESP_ERROR_CHECK(esp_wifi_start());

    // Check whether the WiFi is already connected.
    EventBits_t bits = xEventGroupGetBits(event_group);
    if (bits & WIFI_CONNECTED_BIT) {
      return true;
    }

    if (ssid != nullptr && password != nullptr) {
      wifi_config_t wifi_config;
      bzero(&wifi_config, sizeof(wifi_config));
      wifi_config.sta.threshold.authmode = auth_mode;
      wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
      memset(wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));
      memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
      memset(wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
      memcpy(wifi_config.sta.password, password, strlen(password));
    }
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Wait for the connection to complete for 10 seconds.
    bits = xEventGroupWaitBits(
      event_group,
      WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
      pdFALSE,
      pdFALSE,
      pdMS_TO_TICKS(10000)
    );
    if (bits & WIFI_CONNECTED_BIT) {
      return true;
    }

    return false;
  }

  void Connector::disconnect() {
    // Check whether the WiFi is already connected.
    EventBits_t bits = xEventGroupGetBits(event_group);
    if (bits & WIFI_CONNECTED_BIT) {
      esp_wifi_disconnect();
    }

    xEventGroupClearBits(event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    esp_wifi_stop();

    esp_wifi_deinit();

    auto netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
      esp_netif_destroy(netif);
    }

    if (any_id_handler) {
      esp_event_handler_instance_unregister(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        any_id_handler
      );
      any_id_handler = nullptr;
    }

    if (got_ip_handler) {
      esp_event_handler_instance_unregister(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        got_ip_handler
      );
      got_ip_handler = nullptr;
    }
  }

  const std::string& Connector::getIP() const {
    return ip;
  }

  ////////////////////////////////
  // Private methods

  Connector::Connector()
    : any_id_handler(nullptr)
    , got_ip_handler(nullptr)
  {
    event_group = xEventGroupCreate();
  }

  Connector::~Connector() {
    disconnect();
    vEventGroupDelete(event_group);
  }

  void Connector::wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto self = static_cast<Connector*>(arg);

    if (event_id == WIFI_EVENT_STA_CONNECTED) {
      xEventGroupSetBits(self->event_group, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      xEventGroupSetBits(self->event_group, WIFI_FAIL_BIT);
    }
  }

  void Connector::gotIPEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto self = static_cast<Connector*>(arg);

    if (event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
      ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
      xEventGroupSetBits(self->event_group, WIFI_CONNECTED_BIT);
    }
  }
} // namespace wifi_connect