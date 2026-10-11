#ifdef ESP8266
  #include <ESP8266WiFi.h>
#else // ESP32
  #include <WiFi.h>
  #include <ETH.h>
#endif

#ifndef Network_h
#define Network_h

class WLEDNetworkClass
{
public:
  IPAddress localIP();
  IPAddress subnetMask();
  IPAddress gatewayIP();
  void localMAC(uint8_t* MAC);
  bool isConnected();
  bool isEthernet();

  // Per-interface accessors. WiFi and Ethernet can be up at the same time, in which
  // case the methods above report whichever one has routing priority; these report a
  // specific interface regardless. On builds without Ethernet the ethernet* methods
  // are still present and simply report "down", so callers need no #ifdef.
  IPAddress ethernetIP();
  IPAddress ethernetSubnetMask();
  IPAddress ethernetGatewayIP();
  bool isEthernetUp();

  IPAddress wifiIP();
  IPAddress wifiSubnetMask();
  IPAddress wifiGatewayIP();
  bool isWiFiUp();
};

extern WLEDNetworkClass WLEDNetwork;

#endif