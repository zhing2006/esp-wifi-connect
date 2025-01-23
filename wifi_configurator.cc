#include "wifi_configurator.hh"

#include <cstdio>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <lwip/ip_addr.h>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char done_html_start[] asm("_binary_done_html_start");

namespace wifi_connect {

  #define TAG "wifi_connect::Configurator"

  ////////////////////////////////
  // Public methods

  Configurator& Configurator::getInstance() {
    static Configurator instance;
    return instance;
  }

  void Configurator::setAPSSIDPrefix(std::string ap_ssid_prefix) {
    this->ap_ssid_prefix = std::move(ap_ssid_prefix);
  }

  void Configurator::setAPIP(std::string ap_ip) {
    this->ap_ip = std::move(ap_ip);
  }

  std::string Configurator::getWebServerUrl() const {
    return "http://" + ap_ip;
  }

  void Configurator::start() {
    ESP_ERROR_CHECK(
      esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &Configurator::wifiEventHandler,
        this,
        &any_id_handler
      )
    );

    ESP_ERROR_CHECK(
      esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &Configurator::gotIPEventHandler,
        this,
        &got_ip_handler
      )
    );

    startAP();
    startWebServer();
  }

  void Configurator::stop() {
    if (web_server != nullptr) {
      httpd_stop(web_server);
      web_server = nullptr;
    }

    dns_server.stop();

    esp_wifi_stop();

    esp_wifi_deinit();

    auto netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
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

  ////////////////////////////////
  // Private methods

  Configurator::Configurator()
    : ap_ssid_prefix("ESP32-")
    , any_id_handler(nullptr)
    , got_ip_handler(nullptr)
    , dns_server()
    , web_server(nullptr)
  {
    event_group = xEventGroupCreate();
  }

  Configurator::~Configurator() {
    stop();
    vEventGroupDelete(event_group);
  }

  void Configurator::startAP() {
    // Get the MAC address.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    // Generate the SSID.
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X%02X", ap_ssid_prefix.c_str(), mac[3], mac[4], mac[5]);

    // Create the WiFi access point.
    auto netif = esp_netif_create_default_wifi_ap();

    // Set the router IP address.
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ipaddr_addr(ap_ip.c_str());
    ip_info.gw.addr = ipaddr_addr(ap_ip.c_str());
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(netif);
    esp_netif_set_ip_info(netif, &ip_info);
    esp_netif_dhcps_start(netif);

    // Start the DNS server
    this->dns_server.start(ip_info.gw);

    // Initialize the WiFi stack in Access Point mode.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set the WiFi configuration.
    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.ap.ssid, ssid);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    // Start the WiFi Access Point
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Access point started: SSID=%s, IP=%s", ssid, ap_ip.c_str());
  }

  void Configurator::startWebServer() {
    // Start the web server.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&web_server, &config));

    // Register the index.html file.
    httpd_uri_t index_html = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        httpd_resp_send(req, index_html_start, strlen(index_html_start));
        return ESP_OK;
      },
      .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(web_server, &index_html));

    // Register the /scan URI
    httpd_uri_t scan = {
      .uri = "/scan",
      .method = HTTP_GET,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        esp_wifi_scan_start(nullptr, true);
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_num * sizeof(wifi_ap_record_t));
        esp_wifi_scan_get_ap_records(&ap_num, ap_records);

        // Send the scan results as JSON.
        char buffer[128];
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr_chunk(req, "[");
        for (int i = 0; i < ap_num; i++) {
          ESP_LOGI(TAG, "SSID: %s, RSSI: %d, Authmode: %d",
            (char *)ap_records[i].ssid, ap_records[i].rssi, ap_records[i].authmode);
          snprintf(buffer, sizeof(buffer), "{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":%d}",
            (char *)ap_records[i].ssid, ap_records[i].rssi, ap_records[i].authmode);
          httpd_resp_sendstr_chunk(req, buffer);
          if (i < ap_num - 1) {
            httpd_resp_sendstr_chunk(req, ",");
          }
        }
        httpd_resp_sendstr_chunk(req, "]");
        httpd_resp_sendstr_chunk(req, NULL);
        free(ap_records);
        return ESP_OK;
      },
      .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(web_server, &scan));

    // Register the form submission
    httpd_uri_t form_submit = {
      .uri = "/submit",
      .method = HTTP_POST,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        char buffer[128];
        int ret = httpd_req_recv(req, buffer, sizeof(buffer));
        if (ret <= 0) {
          if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
          }
          return ESP_FAIL;
        }
        buffer[ret] = '\0';
        ESP_LOGI(TAG, "Received form data: %s", buffer);

        std::string decoded = urlDecode(buffer);
        ESP_LOGI(TAG, "Decoded form data: %s", decoded.c_str());

        // Parse the form data.
        char ssid[32], password[64];
        password[0] = '\0';  // Initialize password as empty.

        // First extract SSID
        if (sscanf(decoded.c_str(), "ssid=%32[^&]", ssid) != 1) {
          httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID");
          return ESP_FAIL;
        }

        // Then look for password if present.
        auto pwd_pos = decoded.find("&password=");
        if (pwd_pos != std::string::npos) {
          strncpy(password, decoded.c_str() + pwd_pos + 10, sizeof(password) - 1);
          password[sizeof(password) - 1] = '\0';  // Ensure null termination.
        }

        // Get this object from the user context.
        auto *self = static_cast<Configurator *>(req->user_ctx);
        if (!self->connectToWifi(ssid, password)) {
          char error[] = "Failed to connect to WiFi";
          char location[128];
          snprintf(location, sizeof(location), "/?error=%s&ssid=%s", error, ssid);

          httpd_resp_set_status(req, "302 Found");
          httpd_resp_set_hdr(req, "Location", location);
          httpd_resp_send(req, NULL, 0);
          return ESP_OK;
        }

        // Set HTML response.
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, done_html_start, HTTPD_RESP_USE_STRLEN);

        // Restart after 3 seconds.
        xTaskCreate([](void *self) {
          ESP_LOGI(TAG, "Restarting in 3 seconds...");
          vTaskDelay(pdMS_TO_TICKS(3000));
          esp_restart();
        }, "restart_task", 4096, self, 5, NULL);
        return ESP_OK;
      },
      .user_ctx = this
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(web_server, &form_submit));

    auto captive_portal_handler = [](httpd_req_t *req) -> esp_err_t {
      auto *self = static_cast<Configurator *>(req->user_ctx);
      std::string url = self->getWebServerUrl() + "/";
      // Set content type to prevent browser warnings.
      httpd_resp_set_type(req, "text/html");
      httpd_resp_set_status(req, "302 Found");
      httpd_resp_set_hdr(req, "Location", url.c_str());
      httpd_resp_send(req, NULL, 0);
      return ESP_OK;
    };

    // Register all common captive portal detection endpoints.
    const char* captive_portal_urls[] = {
      "/hotspot-detect.html",     // Apple
      "/generate_204",            // Android
      "/mobile/status.php",       // Android
      "/check_network_status.txt",// Windows
      "/ncsi.txt",                // Windows
      "/fwlink/",                 // Microsoft
      "/connectivity-check.html", // Firefox
      "/success.txt",             // Various
      "/portal.html",             // Various
      "/library/test/success.html"// Apple
    };

    for (const auto& url : captive_portal_urls) {
      httpd_uri_t redirect_uri = {
        .uri = url,
        .method = HTTP_GET,
        .handler = captive_portal_handler,
        .user_ctx = this
      };
      ESP_ERROR_CHECK(httpd_register_uri_handler(web_server, &redirect_uri));
    }

    ESP_LOGI(TAG, "Web server started");
  }

  /// @brief Connect to the WiFi.
  /// @param ssid The SSID.
  /// @param password The password.
  /// @return True if the connection was successful, false otherwise.
  bool Configurator::connectToWifi(const char* ssid, const char* password) {
    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, password);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.failure_retry_cnt = 1;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    auto ret = esp_wifi_connect();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to connect to WiFi, error: %d", ret);
      return false;
    }
    ESP_LOGI(TAG, "Connecting to WiFi %s", ssid);

    // Wait for the connection to complete for 10 seconds.
    EventBits_t bits = xEventGroupWaitBits(event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (bits & WIFI_CONNECTED_BIT) {
      ESP_LOGI(TAG, "Connected to WiFi %s", ssid);
      return true;
    } else {
      ESP_LOGE(TAG, "Failed to connect to WiFi %s", ssid);
      return false;
    }
  }

  std::string Configurator::urlDecode(const std::string &url) {
    std::string decoded;
    for (size_t i = 0; i < url.length(); ++i) {
      if (url[i] == '%') {
        char hex[3];
        hex[0] = url[i + 1];
        hex[1] = url[i + 2];
        hex[2] = '\0';
        char ch = static_cast<char>(std::stoi(hex, nullptr, 16));
        decoded += ch;
        i += 2;
      } else if (url[i] == '+') {
        decoded += ' ';
      } else {
        decoded += url[i];
      }
    }
    return decoded;
  }

  void Configurator::wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto self = static_cast<Configurator*>(arg);

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
      wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
      ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
      wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
      ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
      xEventGroupSetBits(self->event_group, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      xEventGroupSetBits(self->event_group, WIFI_FAIL_BIT);
    }
  }

  void Configurator::gotIPEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto self = static_cast<Configurator*>(arg);

    if (event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
      ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
      xEventGroupSetBits(self->event_group, WIFI_CONNECTED_BIT);
    }
  }

} // namespace wifi_connect