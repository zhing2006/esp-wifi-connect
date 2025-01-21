#ifndef __DNS_SERVER_HH__
#define __DNS_SERVER_HH__

#include <esp_netif_ip_addr.h>

typedef struct tskTaskControlBlock * TaskHandle_t;

namespace wifi_connect {

  /// @brief The DNS server class.
  class DNSServer {
  public:
    /// @brief The constructor.
    DNSServer();
    /// @brief The destructor.
    ~DNSServer();

    /// @brief Start the DNS server.
    /// @param gateway The gateway IP address.
    void start(const esp_ip4_addr_t& gateway);

    /// @brief Stop the DNS server.
    void stop();

  private:
    /// @brief The DNS port.
    int port;
    /// @brief The DNS server socket.
    int server_socket;
    /// @brief The gateway IP address.
    esp_ip4_addr_t gateway;
    /// @brief The DNS server task handle.
    TaskHandle_t task_handle;

    /// @brief The DNS server task.
    void run();
  };

}

#endif // __DNS_SERVER_HH__