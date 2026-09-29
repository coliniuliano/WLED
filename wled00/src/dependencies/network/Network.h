#ifdef ESP8266
  #include <ESP8266WiFi.h>
#else // ESP32
  #include <WiFi.h>
  // MUST match the ETH selection in wled.h. Network.cpp does not include wled.h,
  // so without this it binds to arduino's stock `extern ETHClass ETH` -- an object
  // ETH.begin() was never called on, whose handle and netif are NULL. Everything
  // here then reports the interface as down while the real one is up.
  #if defined(ARDUINO_ARCH_ESP32S3) && defined(WLED_USE_ETHERNET)
    #include <ETHClass2.h>
    #define ETH ETH2
  #else
    #include <ETH.h>
  #endif
#endif

#ifndef Network_h
#define Network_h

class NetworkClass
{
public:
  // Primary interface methods (Ethernet priority, backwards compatible)
  IPAddress localIP();
  IPAddress subnetMask();
  IPAddress gatewayIP();
  void localMAC(uint8_t* MAC);
  bool isConnected();
  bool isEthernet();

  // Dual-interface methods - query specific interfaces
  IPAddress ethernetIP();
  IPAddress ethernetSubnetMask();
  IPAddress ethernetGatewayIP();
  bool isEthernetUp();

  IPAddress wifiIP();
  IPAddress wifiSubnetMask();
  IPAddress wifiGatewayIP();
  bool isWiFiUp();
};

extern NetworkClass Network;

#endif