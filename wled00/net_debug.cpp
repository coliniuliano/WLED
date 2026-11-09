#include "wled.h"

#ifdef WLED_DEBUG_HOST

size_t NetworkDebugPrinter::write(uint8_t c) {
  if (!WLED_CONNECTED || !netDebugEnabled) return 0;

  if (!debugPrintHostIP && !debugPrintHostIP.fromString(netDebugPrintHost)) {
    #ifdef ESP8266
      WiFi.hostByName(netDebugPrintHost, debugPrintHostIP, 750);
    #else
      // DNS is resolved by lwIP against the resolvers of whichever interface is up, so
      // the lookup is not interface-specific: NetworkManager::hostByName serves both
      // WiFi and Ethernet. ETHClass has no hostByName() in arduino-esp32 3.x, so the
      // former #ifdef WLED_USE_ETHERNET branch did not compile.
      Network.hostByName(netDebugPrintHost, debugPrintHostIP);
    #endif
  }

  debugUdp.beginPacket(debugPrintHostIP, netDebugPrintPort);
  debugUdp.write(c);
  debugUdp.endPacket();
  return 1;
}

size_t NetworkDebugPrinter::write(const uint8_t *buf, size_t size) {
  if (!WLED_CONNECTED || buf == nullptr || !netDebugEnabled) return 0;

  if (!debugPrintHostIP && !debugPrintHostIP.fromString(netDebugPrintHost)) {
    #ifdef ESP8266
      WiFi.hostByName(netDebugPrintHost, debugPrintHostIP, 750);
    #else
      // DNS is resolved by lwIP against the resolvers of whichever interface is up, so
      // the lookup is not interface-specific: NetworkManager::hostByName serves both
      // WiFi and Ethernet. ETHClass has no hostByName() in arduino-esp32 3.x, so the
      // former #ifdef WLED_USE_ETHERNET branch did not compile.
      Network.hostByName(netDebugPrintHost, debugPrintHostIP);
    #endif
  }

  debugUdp.beginPacket(debugPrintHostIP, netDebugPrintPort);
  size = debugUdp.write(buf, size);
  debugUdp.endPacket();
  return size;
}

NetworkDebugPrinter NetDebug;

#endif
