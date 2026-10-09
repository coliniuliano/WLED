> ## ⚠️ This branch is out of date — superseded by `w5500-ethernet-espidf5`
>
> **Status:** frozen reference. Known-working on hardware, but no longer the branch to build from.
>
> This branch forked from upstream WLED at `cc5b5047` (2025-12-03, `0.16.0-alpha`). Upstream has since
> moved **1060+ commits** ahead, shipped **v16.0.0**, and is now on `17.0.0-devV5`. Critically, ESP32
> builds migrated from **ESP-IDF 4.4 / arduino-esp32 2.0.9** to **ESP-IDF 5.x / arduino-esp32 3.x**.
>
> That migration makes this branch a dead end rather than something to merge forward:
>
> - The `espressif32 @ 5.4.0` platform pin here does not survive the jump.
> - Both library forks this branch depends on — [`ETHClass2`](https://github.com/coliniuliano/ETHClass2)
>   and [`TroyHacksAsyncUDP`](https://github.com/coliniuliano/TroyHacksAsyncUDP) — exist *only* to work
>   around IDF 4.4 limitations. arduino-esp32 3.x has **native W5500 SPI Ethernet** in the stock `ETH`
>   class, so they become unnecessary rather than needing porting.
> - Upstream renamed `Network` → `WLEDNetwork` and ran a sweeping "encapsulate globals as static"
>   refactor across exactly the files this branch modifies (`network.cpp`, `e131.cpp`, `util.cpp`,
>   `set.cpp`, `wled_server.cpp`, `wled.h`). A textual merge resolves cleanly and then fails to
>   compile — the worst outcome, because it looks finished.
>
> **Work continues on [`w5500-ethernet-espidf5`](../../tree/w5500-ethernet-espidf5)**, re-implemented
> from scratch on current upstream `main` against the native IDF5 SPI Ethernet driver. The design below
> carried over; the code did not. See `MIGRATION-NOTES.md` on that branch for the decision log.
>
> Two tags pin this branch's history in place: `attempt1-base` (the fork point) and `attempt1-working`
> (the last known-good-on-hardware commit), so this reference survives even if the branch is deleted.

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


# TODO

- more control over serial port
  - set up wifi and ip addresses
- no access point?
   - wifi is shit on this board, cant get AP and cant connect to wifi

- cant do dhcp AND have a static address in software
   CONFIG_LWIP_AUTOIP is not set — so the textbook answer is off the table for you.
   lwIP has an AUTOIP module with a LWIP_DHCP_AUTOIP_COOP mode built for exactly this: it holds a 169.254 address while the DHCP client keeps discovering in the background, then switches cleanly when a lease arrives. But it isn't compiled into the prebuilt lwIP that arduino-esp32 ships, so you'd need a custom framework build to get it.