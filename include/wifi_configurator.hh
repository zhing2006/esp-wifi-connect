#ifndef __CONFIGURATOR_HH__
#define __CONFIGURATOR_HH__

#include <string>

#include "esp_event.h"

#include "dns_server.hh"

namespace wifi_connect {

  /// @brief The Configurator class is responsible for configuring the WiFi connection.
  class Configurator {
  public:
    /// @brief Get the instance of the Configurator.
    /// @return The instance of the Configurator.
    static Configurator& getInstance();

    /// @brief Set the access point SSID prefix.
    /// @param ap_ssid_prefix The access point SSID prefix.
    void setAPSSIDPrefix(std::string ap_ssid_prefix);

    /// @brief Set the access point IP.
    /// @param ap_ip The access point IP.
    void setAPIP(std::string ap_ip);

    /// @brief Start the configuration process.
    void start();

    /// @brief Stop the configuration process.
    void stop();

  private:
    /// @brief The constructor.
    Configurator();
    /// @brief The destructor.
    ~Configurator();

    /// @brief Start the access point.
    void startAP();

    /// @brief The access point SSID.
    std::string ap_ssid_prefix;
    /// @brief The access point IP.
    std::string ap_ip;
    /// @brief The any id event handler.
    esp_event_handler_instance_t any_id_handler;
    /// @brief The got ip event handler.
    esp_event_handler_instance_t got_ip_handler;
    /// @brief The DNS server.
    DNSServer dns_server;

    /// @brief The WiFi event handler.
    /// @param arg The user argument.
    /// @param event_base The event object.
    /// @param event_id The event id.
    /// @param event_data The event data.
    static void wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
  };

} // namespace wifi_connect

#endif // __CONFIGURATOR_HH__