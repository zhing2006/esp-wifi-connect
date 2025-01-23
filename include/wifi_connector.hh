#ifndef __WIFI_CONNECTOR_H__
#define __WIFI_CONNECTOR_H__

#include <string>

#include <esp_event.h>
#include <esp_wifi.h>

namespace wifi_connect {

  /// @brief The WiFi connector.
  class Connector {
  public:
    /// @brief Get the instance of the connector.
    /// @return The instance of the connector.
    static Connector& getInstance();

    /// @brief Connect to the WiFi.
    /// Any the SSID and password can be null.
    /// In this case, the connector will try to connect to the last known network.
    /// @param auth_mode The authentication mode.
    /// @param ssid The SSID.
    /// @param password The password.
    /// @return True if the connection was successful, false otherwise.
    bool connect(wifi_auth_mode_t auth_mode, const char* ssid = nullptr, const char* password = nullptr);

    /// @brief Disconnect from the WiFi.
    void disconnect();

    /// @brief Get the IP address.
    /// @return The IP address.
    const std::string& getIP() const;

  private:

    /// @brief The constructor.
    Connector();
    /// @brief The destructor.
    ~Connector();

    /// @brief The WiFi event group.
    EventGroupHandle_t event_group;
    /// @brief The any id event handler.
    esp_event_handler_instance_t any_id_handler;
    /// @brief The got ip event handler.
    esp_event_handler_instance_t got_ip_handler;
    /// @brief The IP address.
    std::string ip;

    /// @brief The WiFi event handler.
    /// @param arg The user argument.
    /// @param event_base The event object.
    /// @param event_id The event id.
    /// @param event_data The event data.
    static void wifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    /// @brief The got IP event handler.
    /// @param arg The user argument.
    /// @param event_base The event object.
    /// @param event_id The event id.
    /// @param event_data The event data.
    static void gotIPEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
  };

}

#endif // __WIFI_CONNECTOR_H__