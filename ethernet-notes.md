## Status

- Working! with specific configuration (eg no Wifi because it steamrolls the ETH)

## Setup

- No additional components, just hardwire the W5500 module into the hardcoded pins in network.cpp
- Do not connect Wifi since Wifi and ETH share a static IP address which will never work
- Wifi appears to have priority over ETH
- Connect to a switch (no internet, no DHCP)
  - `ETH-E: Connected` message appears in debug mode
- Connect to the WLED-AP access point and set a static IP address
  - 169.254.x.x is the Automatic Private IP Addressing (APIPA) space
  - 169.254.120.123 is what I used
  - Gateway 169.254.1.1
  - Subnet 255.255.0.0
- Connect laptop to the switch too
  - Shows up in network settings as Self-assigned IP (169.254.129.160 in this case)
- Visit WLED address 169.254.120.123 in a browser, WLED page loads through the switch

## Things to do

- Document decisions (like why we needed to use non-Tasmota platform)
  - ETHClass2 calls w5500 functions that are not compiled into Tasmota version
  - ESP-IDF minimum 4.4ish for those functions
- Separate Wifi config from ETH config so that both can coexist
  - Wifi currently takes over from ETH, at least when there is no DHCP
- Look into DHCP and automatically assigning an IP in the 169.254 range
- Test actual LEDs
- Test ArtNet or sACN with actual LEDs

## Intermittent

Upon reconnecting the circuit, computer is unable to access 169.254.120.123
For some reason it was unable to route packets to that address
Changing to .124 appears to work
The laptop was also given a static IP instead of trying DHCP





🎯 Why ETHClass2 is MUCH BETTER

  The Key Advantage

  ETHClass2 is a drop-in replacement for the standard ESP32 ETH library that adds W5500 (SPI) support while keeping the exact same API!

  WLED already uses:
  - ETH.begin() (network.cpp:224)
  - ETH.config() (network.cpp:400)
  - ETH.setHostname() (network.cpp:407)
  - #include <ETH.h> (wled.h:96)

  With ETHClass2, you don't need to change ANY of this code! You just:
  1. Replace the ETH library with ETHClass2
  2. Add W5500 pin definitions
  3. Change the begin() call parameters

  What ETHClass2 Supports

  From the header file I just examined:
  - ✅ W5500 (SPI Ethernet) - YOUR USE CASE
  - ✅ LAN8720, RTL8201, etc. (RMII) - existing WLED boards
  - ✅ ESP32-S3 support
  - ✅ Same API as native ETH class
  - ✅ IPv4 and IPv6
  - ✅ All existing ETH. methods work unchanged

  ---
  📝 REVISED Implementation Plan (Much Simpler!)

⏺ Phase 1: Add ETHClass2 Library

  Modify platformio.ini (around line 299 in [esp32_idf_V4] section):

  lib_deps =
    ${esp32_all_variants.lib_deps}
    https://github.com/someweisguy/esp_dmx.git#47db25d
    https://github.com/troyhacks/ETHClass2.git  # ADD THIS LINE
    ${env.lib_deps}

  That's it for dependencies! No other libraries needed.

  ---
  Phase 2: Update Board Definitions

  Modify /wled00/const.h (add after line 383):

  #define WLED_ETH_W5500_ESP32S3    13  // W5500 SPI for ESP32-S3
  #define WLED_NUM_ETH_TYPES        14

  Modify /wled00/wled_ethernet.h - add SPI Ethernet struct:

  #ifdef WLED_USE_ETHERNET

  // Existing RMII struct stays the same...

  // NEW: SPI Ethernet settings for W5500
  typedef struct SpiEthernetSettings {
    int8_t eth_miso;
    int8_t eth_mosi;
    int8_t eth_sck;
    int8_t eth_cs;
    int8_t eth_int;   // -1 if unused
    int8_t eth_rst;   // -1 if unused
    uint8_t eth_phy_addr;  // Usually 1 for W5500
    eth_phy_type_t eth_type;  // ETH_PHY_W5500
  } spi_ethernet_settings;

  extern const spi_ethernet_settings spiEthernetBoards[];

  #endif

  ---
  Phase 3: Update network.cpp

  Add board configuration (after line 148):

  // SPI Ethernet boards (W5500, etc.)
  const spi_ethernet_settings spiEthernetBoards[] = {
    // WLED_ETH_W5500_ESP32S3 (index 13)
    {
      13,        // MISO - adjust for your board
      11,        // MOSI - adjust for your board
      12,        // SCK  - adjust for your board
      10,        // CS   - adjust for your board
      -1,        // INT (not used)
      14,        // RST  - adjust for your board
      1,         // PHY addr (usually 1 for W5500)
      ETH_PHY_W5500
    }
  };

  Update initEthernet() function (replace/modify around line 150):

  bool initEthernet()
  {
    static bool successfullyConfiguredEthernet = false;

    if (successfullyConfiguredEthernet) return false;
    if (ethernetType == WLED_ETH_NONE) return false;
    if (ethernetType >= WLED_NUM_ETH_TYPES) return false;

    DEBUG_PRINTF_P(PSTR("initE: Attempting ETH config: %d\n"), ethernetType);

    // Check if SPI Ethernet (W5500)
    if (ethernetType == WLED_ETH_W5500_ESP32S3) {
      spi_ethernet_settings es = spiEthernetBoards[0];  // First SPI board

      // Allocate SPI pins
      managed_pin_type pinsToAllocate[] = {
        { es.eth_miso, false },
        { es.eth_mosi, true },
        { es.eth_sck,  true },
        { es.eth_cs,   true },
        { es.eth_rst,  true }
      };

      if (!PinManager::allocateMultiplePins(pinsToAllocate, 5, PinOwner::Ethernet)) {
        DEBUG_PRINTLN(F("initE: Failed to allocate SPI pins"));
        return false;
      }

      // Initialize SPI
      SPI.begin(es.eth_sck, es.eth_miso, es.eth_mosi, es.eth_cs);

      // Hardware reset if pin defined
      if (es.eth_rst >= 0) {
        pinMode(es.eth_rst, OUTPUT);
        digitalWrite(es.eth_rst, LOW);
        delay(50);
        digitalWrite(es.eth_rst, HIGH);
        delay(200);
      }

      // THE MAGIC: ETHClass2 supports W5500 with same ETH.begin() API!
      if (!ETH.begin(
            es.eth_phy_addr,    // PHY address
            es.eth_rst,         // Reset pin
            es.eth_miso,        // MISO
            es.eth_mosi,        // MOSI
            es.eth_sck,         // SCK
            es.eth_cs,          // CS
            es.eth_type,        // ETH_PHY_W5500
            (eth_clock_mode_t)ETH_CLOCK_GPIO0_IN,  // Ignored for SPI
            SPI                 // SPI object
          )) {
        DEBUG_PRINTLN(F("initE: ETH.begin() failed for W5500"));
        for (auto mpt : pinsToAllocate) {
          PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
        }
        return false;
      }

      successfullyConfiguredEthernet = true;
      DEBUG_PRINTLN(F("initE: W5500 successfully configured!"));
      return true;
    }

    // EXISTING RMII CODE CONTINUES HERE (lines 152-243)
    // ... all the existing ethernetBoards[] stuff for LAN8720, etc.

    ethernet_settings es = ethernetBoards[ethernetType];
    // ... rest of existing function unchanged ...
  }

  ---
  Phase 4: Build Environment

  Add to platformio.ini (around line 632):

  [env:esp32s3_w5500]
  ;; ESP32-S3 with W5500 SPI Ethernet using ETHClass2
  board = esp32-s3-devkitc-1
  platform = ${esp32s3.platform}
  platform_packages = ${esp32s3.platform_packages}
  upload_speed = 921600
  build_unflags = ${common.build_unflags}
  build_flags =
    ${common.build_flags}
    ${esp32s3.build_flags}
    -D WLED_RELEASE_NAME=\"ESP32-S3_W5500\"
    -D WLED_USE_ETHERNET
    -D WLED_ETH_DEFAULT=13           ; W5500 type
    -D ARDUINO_USB_CDC_ON_BOOT=1
    -D ARDUINO_USB_MODE=1
    -D WLED_WATCHDOG_TIMEOUT=0
  lib_deps =
    ${esp32s3.lib_deps}
    https://github.com/troyhacks/ETHClass2.git
  board_build.partitions = ${esp32.default_partitions}
  board_build.f_flash = 80000000L
  board_build.flash_mode = qio
  monitor_filters = esp32_exception_decoder

  ---
  Phase 5: Enable for ESP32-S3

  Modify /wled00/wled.h (around line 387):

  #if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
    // Allow Ethernet on ESP32 classic AND ESP32-S3 (with ETHClass2 + W5500)
    #if !defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
      #error "Ethernet only supported on ESP32 and ESP32-S3"
    #endif
    #include <ETH.h>  // Will use ETHClass2 when library is included
    // ... rest of existing ethernet code ...
  #endif

  ---
  🎉 Key Benefits of ETHClass2 Approach

  1. ✅ Zero API changes - All existing ETH.method() calls work unchanged
  2. ✅ Works with existing WLED network code - Same events, same config
  3. ✅ Drop-in replacement - Just change the library dependency
  4. ✅ Supports BOTH RMII and SPI - Can still build for LAN8720 boards
  5. ✅ mDNS works - Since it uses the same ETH class, mDNS will work (your note about W5500 not supporting mDNS is resolved!)
  6. ✅ Minimal code changes - Maybe 50 lines total

  ---
  🚀 What You Need to Do

  1. Determine your ESP32-S3 + W5500 pin connections
    - What GPIO for MISO, MOSI, SCK, CS, RST?
  2. Choose implementation approach:
    - Option A: I implement this for you right now
    - Option B: I create a minimal test sketch first to verify your hardware
    - Option C: You tell me your pins and I'll hardcode everything