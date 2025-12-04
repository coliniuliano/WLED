#ifdef ESP8266
  #include <ESP8266WiFi.h>
#else // ESP32
  #include <WiFi.h>
  #include <ETH.h>
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