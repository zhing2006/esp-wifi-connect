#include "dns_server.hh"

#include <esp_log.h>

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace wifi_connect {

  #define TAG "wifi_connect::DNSServer"

  ////////////////////////////////
  // Public methods

  DNSServer::DNSServer()
    : port(53)
    , server_socket(-1)
    , task_handle(nullptr)
  {}

  DNSServer::~DNSServer() {
    stop();
  }

  void DNSServer::start(const esp_ip4_addr_t& gateway) {
    this->gateway = gateway;
    this->server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (this->server_socket < 0) {
      ESP_LOGE(TAG, "Failed to create the DNS server socket");
      return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(this->server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      ESP_LOGE(TAG, "Failed to bind the DNS server socket to port %d", port);
      close(this->server_socket);
      return;
    }

    // Start the DNS server task.
    xTaskCreate(
      [](void* arg) {
        auto dns_server = static_cast<DNSServer*>(arg);
        dns_server->run();
      },
      "dns_server",
      4096,
      this,
      5,
      &this->task_handle
    );
  }

  void DNSServer::stop() {
    // Stop the DNS server task.
    if (this->task_handle) {
      vTaskDelete(this->task_handle);
      this->task_handle = nullptr;
    }

    // Close the DNS server socket.
    if (this->server_socket >= 0) {
      close(this->server_socket);
      this->server_socket = -1;
    }
  }

  ////////////////////////////////
  // Private methods

  void DNSServer::run() {
    char buffer[512];
    while (true) {
      struct sockaddr_in client_addr;
      socklen_t client_addr_len = sizeof(client_addr);
      int len = recvfrom(this->server_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_addr_len);
      if (len < 0) {
        ESP_LOGE(TAG, "Failed to receive data from the DNS client.");
        continue;
      }

      // Simple DNS response: point all requests to the gateway.
      buffer[2] |= 0x80; // Set the response flag.
      buffer[3] |= 0x80; // Set the recursion available.
      buffer[7] = 1; // Set the answer count to 1.

      // Add answer section.
      memcpy(&buffer[len], "\xC0\x0C", 2); // Pointer to the domain name.
      len += 2;
      memcpy(&buffer[len], "\x00\x01\x00\x01\x00\x00\x00\x1C\x00\x04", 10); // Type A, Class IN, TTL 28s, Data length 4.
      len += 10;
      memcpy(&buffer[len], &this->gateway.addr, 4); // Gateway IP address.
      len += 4;

      if (sendto(this->server_socket, buffer, len, 0, (struct sockaddr*)&client_addr, client_addr_len) < 0) {
        ESP_LOGE(TAG, "Failed to send data to the DNS client.");
      }
    }
  }

}