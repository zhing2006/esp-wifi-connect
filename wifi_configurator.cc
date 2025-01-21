#include "wifi_configurator.hh"

#include <cstdio>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <lwip/ip_addr.h>

extern const char index_html_start[] asm("_binary_index_html_start");

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
        &Configurator::wifiEventHandler,
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
  {}

  Configurator::~Configurator() {
    stop();
  }

  void Configurator::startAP() {
    // Get the MAC address.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    // Generate the SSID.
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X%02X", ap_ssid_prefix.c_str(), mac[3], mac[4], mac[5]);

    // Initialize the TCP/IP stack.
    ESP_ERROR_CHECK(esp_netif_init());

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
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Access point started: SSID=%s, IP=%s", ssid, ap_ip.c_str());
  }

  void Configurator::startWebServer() {
    // Start the web server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&web_server, &config));

    // Register the index.html file
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

    auto captive_portal_handler = [](httpd_req_t *req) -> esp_err_t {
      auto *self = static_cast<Configurator *>(req->user_ctx);
      std::string url = self->getWebServerUrl() + "/";
      // Set content type to prevent browser warnings
      httpd_resp_set_type(req, "text/html");
      httpd_resp_set_status(req, "302 Found");
      httpd_resp_set_hdr(req, "Location", url.c_str());
      httpd_resp_send(req, NULL, 0);
      return ESP_OK;
    };

    // Register all common captive portal detection endpoints
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

  void Configurator::wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    // auto configurator = static_cast<Configurator*>(arg);

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
      wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
      ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
      wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
      ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
    }
  }

} // namespace wifi_connect