#include "Network.h"

IPAddress NetworkClass::localIP()
{
  IPAddress localIP;
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  if (ETH.linkUp()) {
    localIP = ETH.localIP();
    if (localIP[0] != 0) {
      return localIP;
    }

    // ETH.localIP() returns 0.0.0.0 but link is up - check if static IP is configured
    extern IPAddress ethernetStaticIP;
    extern unsigned long ethernetDhcpStartTime;
    if (ethernetStaticIP != (uint32_t)0x00000000 && ethernetDhcpStartTime == 0) {
      // Static IP configured, return it
      return ethernetStaticIP;
    }
  }
#endif
  localIP = WiFi.localIP();
  if (localIP[0] != 0) {
    return localIP;
  }

  return INADDR_NONE;
}

IPAddress NetworkClass::subnetMask()
{
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  if (ETH.linkUp()) {
    if (ETH.localIP()[0] != 0) {
      return ETH.subnetMask();
    }

    // ETH.localIP() returns 0.0.0.0 but link is up - check if static subnet is configured
    extern IPAddress ethernetStaticIP;
    extern IPAddress ethernetStaticSN;
    extern unsigned long ethernetDhcpStartTime;
    if (ethernetStaticIP != (uint32_t)0x00000000 && ethernetDhcpStartTime == 0) {
      return ethernetStaticSN;
    }
  }
#endif
  if (WiFi.localIP()[0] != 0) {
    return WiFi.subnetMask();
  }
  return IPAddress(255, 255, 255, 0);
}

IPAddress NetworkClass::gatewayIP()
{
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  if (ETH.linkUp()) {
    if (ETH.localIP()[0] != 0) {
      return ETH.gatewayIP();
    }

    // ETH.localIP() returns 0.0.0.0 but link is up - check if static gateway is configured
    extern IPAddress ethernetStaticIP;
    extern IPAddress ethernetStaticGW;
    extern unsigned long ethernetDhcpStartTime;
    if (ethernetStaticIP != (uint32_t)0x00000000 && ethernetDhcpStartTime == 0) {
      return ethernetStaticGW;
    }
  }
#endif
  if (WiFi.localIP()[0] != 0) {
      return WiFi.gatewayIP();
  }
  return INADDR_NONE;
}

void NetworkClass::localMAC(uint8_t* MAC)
{
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  // ETH.macAddress(MAC); // Does not work because of missing ETHClass:: in ETH.ccp

  // Start work around
  String macString = ETH.macAddress();
  char macChar[18];
  char * octetEnd = macChar;

  strlcpy(macChar, macString.c_str(), 18);

  for (uint8_t i = 0; i < 6; i++) {
    MAC[i] = (uint8_t)strtol(octetEnd, &octetEnd, 16);
    octetEnd++;
  }
  // End work around

  for (uint8_t i = 0; i < 6; i++) {
    if (MAC[i] != 0x00) {
      return;
    }
  }
#endif
  WiFi.macAddress(MAC);
  return;
}

bool NetworkClass::isConnected()
{
  return (WiFi.localIP()[0] != 0 && WiFi.status() == WL_CONNECTED) || isEthernet();
}

bool NetworkClass::isEthernet()
{
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  // Check if link is up first
  if (!ETH.linkUp()) return false;

  // If ETH.localIP() reports an IP, we're definitely connected
  if (ETH.localIP()[0] != 0) return true;

  // ETH.localIP() might return 0.0.0.0 with some drivers (like W5500) even when static IP is configured
  // Check if we have a static IP configured - if so, trust the link status
  extern IPAddress ethernetStaticIP;
  extern unsigned long ethernetDhcpStartTime;
  if (ethernetStaticIP != (uint32_t)0x00000000 && ethernetDhcpStartTime == 0) {
    // Static IP is configured (ethernetDhcpStartTime == 0 means not using DHCP)
    return true;
  }

  return false;
#endif
  return false;
}

NetworkClass Network;