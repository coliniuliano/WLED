# WLED modified for W5500 support for sACN/E1.31 lighting

The goal of this project is to use LED strips with sACN, ArtNet, or whatever DMX protocol while
simultaneously getting the benefits of WLED while DMX is not in use.

This setup allows a W5500 module to work with an ESP32-S3 to receive DMX (and power) over ethernet.

## Setup (my specific case)

- No additional components, just hardwire the W5500 module into the hardcoded pins in network.cpp
- Connect to a switch (no internet, no DHCP)
  - `ETH-E: Connected` message appears in debug mode
- Connect laptop to the switch too
  - Shows up in network settings as Self-assigned IP (169.254.x.x)
- Visit WLED address 169.254.y.y in a browser, WLED page loads through the switch
- Note you can use Wifi and ethernet simulateously

## Changes from stock

### Ethernet Support Additions

1. **Separate Ethernet Static IP Configuration** (wled.h, cfg.cpp, set.cpp, xml.cpp, settings_wifi.htm)
   - Allows setting a separate IP for ethernet and wifi so ethernet can be on a non-DHCP network with a static address but wifi can use DHCP
   - Added `ethernetStaticIP`, `ethernetStaticGW`, `ethernetStaticSN` globals
   - Ethernet now has independent IP config from WiFi
   - Web UI includes Ethernet IP settings fields

2. **Ultra-Fast Link-Local Fallback** (wled.cpp, wled.h, network.cpp)
   - Use an auto-assigned IP quickly and then check for DHCP after (intended to use on an unmanaged switch without internet)
   - 250ms DHCP timeout (was 3s) for instant link-local assignment
   - Exponential backoff DHCP retries: 500ms → 1s → 2s → 4s → 12s
   - Automatic APIPA (169.254.x.x) when DHCP unavailable
   - Supports static IPs with no gateway (0.0.0.0) for link-local

3. **W5500 Static IP Detection Fix** (Network.cpp)
   - Mostly just to fix debug logs which claimed unconnected when ethernet was on a static or self-assigned IP
   - W5500 driver returns 0.0.0.0 from ETH.localIP() even when configured
   - Added fallback to configured static IP values
   - Enhanced isEthernet(), localIP(), subnetMask(), gatewayIP() with workarounds

4. **Dual-Interface WiFi + Ethernet** (network.cpp, Network.h, Network.cpp, wled.cpp)
   - Allows ethernet and wifi to coexist
   - Removed WiFi.disconnect() on Ethernet connection
   - Both interfaces operate simultaneously
   - Added interface-specific methods: ethernetIP(), wifiIP(), isEthernetUp(), isWiFiUp()
   - Primary interface priority: Ethernet preferred if both active
   - Enhanced debug output shows both interfaces

5. **Link-Local IP Trusted Subnet** (wled_server.cpp)
   - Prevents PIN requirement for link-local connections which gave Access Denied errors
   - Added 169.254.0.0/16 to trusted subnet list

6. **Instant IP Reconfiguration** (set.cpp)
   - Prevents need to restart when updating ethernet config
   - Ethernet IP changes apply immediately via ETH.config()
   - Only reboots when Ethernet type changes (hardware init)
   - Minimizes service disruption

### Platform Configuration

- **platformio.ini**: Using non-Tasmota ESP32 platform (espressif32 @ 5.4.0)
  - Required for W5500 functions not in Tasmota build
  - ESP-IDF 4.4+ minimum for ETHClass2 support

