# esp-wifi-connect

[![License](https://img.shields.io/badge/License-GPL3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)

[English](README.md) | [中文](README_CN.md)

## Introduction
This is a component designed for the ESP32 series microcontrollers for simple WiFi provisioning.

## Development
ESP-IDF version 5.3.2 or above.

## Features
- [x] Enable WiFi AP mode and access the provisioning interface via the web.
- [x] Enable WiFi STA mode to connect directly to a WiFi network.
- [x] Support for Personal authentication mode.
- [ ] Support for Enterprise authentication mode.

## Usage
1. Initialize NVS.
```c
/// @brief Initialize NVS.
static void nvs_initialize() {
  auto err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}
```

2. Initialize the event loop.
```c
  // Initialize the event loop.
  ESP_ERROR_CHECK(esp_event_loop_create_default());
```

3. Check if there is previously stored WiFi connection information, if so, connect directly.
```c
  if (Connector::getInstance().isStored()) {
    Connector::getInstance().connect(WIFI_AUTH_WPA_WPA2_PSK)
  }
```

4. If you need to enable AP mode for provisioning.
```c
  // Set the AP node name prefix (the full AP node name will include the last three bytes of the device MAC address in HEX).
  Configurator::getInstance().setAPSSIDPrefix("NAGI-");
  // Set the IP address of the provisioning web service.
  Configurator::getInstance().setAPIP("192.168.123.1");
  // Start the provisioning service.
  Configurator::getInstance().start();
  // Get the AP node name.
  auto ssid = Configurator::getInstance().getAPSSID();
  // Get the provisioning web service URL.
  auto web = Configurator::getInstance().getWebServerUrl();
```

5. At this point, use an Android, iOS, Windows, or Mac device to connect to the above AP node and access the URL to perform WiFi provisioning.

6. After provisioning is complete, the device will restart in 3 seconds, and Connector::getInstance().isStored() will return true.