# esp-wifi-connect

[![License](https://img.shields.io/badge/License-GPL3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)

[English](README.md) | [中文](README_CN.md)

## 介绍
这是为ESP32系列微处理器设计的，用于简单WiFi配网的组件。

## 开发
ESP-IDF v5.3.2以上版本。

## 功能
- [x] 开启WiFi AP模式，通过Web访问配网界面进行WiFi配网。
- [x] 开启WiFi STA模式，直接连接到WiFi网络。
- [x] 支持Personl认证授权模式。
- [ ] 支持Enterprise认证授权模式。

## 使用
1. 初始化好NVS
```c
/// @brief 初始化NVS。
static void nvs_initialize() {
  auto err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}
```

2. 初始化好event loop
```c
  // 初始化event loop。
  ESP_ERROR_CHECK(esp_event_loop_create_default());
```

3. 检测是否有上一次保留的WiFi连接信息，如果有则可以直接建立连接
```c
  if (Connector::getInstance().isStored()) {
    Connector::getInstance().connect(WIFI_AUTH_WPA_WPA2_PSK)
  }
```

4. 如果需要启用AP模式配网
```c
  // 设置AP节点的名字前缀（完整AP节点名会包含设备MAC地址的后三个字节的HEX字符）。
  Configurator::getInstance().setAPSSIDPrefix("NAGI-");
  // 设置配网Web服务的IP地址。
  Configurator::getInstance().setAPIP("192.168.123.1");
  // 启动配网服务。
  Configurator::getInstance().start();
  // 获取AP节点名。
  auto ssid = Configurator::getInstance().getAPSSID();
  // 获取配网Web服务URL。
  auto web = Configurator::getInstance().getWebServerUrl();
```

5. 此时使用Android、iOS、Windows或者Mac设备连接以上AP节点，访问URL则可以进行WiFi配网

6. 完成配网后设备将会在3秒后重启，此时Connector::getInstance().isStored()将返回true