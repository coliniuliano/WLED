#include "wled.h"
#include "fcn_declare.h"
#include "wled_ethernet.h"
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
#include "esp_mac.h"   // esp_read_mac()/ESP_MAC_ETH, for the link-local host part
#endif


#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)

#ifdef WLED_ETH_HAS_SPI
// SPI-attached PHY pins (see wled_ethernet.h). None of the RMII pins below apply.
const managed_pin_type esp32_spi_ethernet_pins[WLED_ETH_SPI_PINS_COUNT] = {
    { WLED_ETH_SPI_MISO, false },
    { WLED_ETH_SPI_MOSI, true  },
    { WLED_ETH_SPI_SCLK, true  },
    { WLED_ETH_SPI_CS,   true  },
    { WLED_ETH_SPI_IRQ,  false },
    { WLED_ETH_SPI_RST,  true  },
};
#endif

#ifdef WLED_ETH_HAS_RMII // RMII options are only valid for classic esp32
// The following six pins are neither configurable nor
// can they be re-assigned through IOMUX / GPIO matrix.
// See https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-ethernet-kit-v1.1.html#ip101gri-phy-interface
const managed_pin_type esp32_nonconfigurable_ethernet_pins[WLED_ETH_RSVD_PINS_COUNT] = {
    { 21, true  }, // RMII EMAC TX EN  == When high, clocks the data on TXD0 and TXD1 to transmitter
    { 19, true  }, // RMII EMAC TXD0   == First bit of transmitted data
    { 22, true  }, // RMII EMAC TXD1   == Second bit of transmitted data
    { 25, false }, // RMII EMAC RXD0   == First bit of received data
    { 26, false }, // RMII EMAC RXD1   == Second bit of received data
    { 27, true  }, // RMII EMAC CRS_DV == Carrier Sense and RX Data Valid
};

const ethernet_settings ethernetBoards[] = {
  // None
  {
  },

  // WT32-EHT01
  // Please note, from my testing only these pins work for LED outputs:
  //   IO2, IO4, IO12, IO14, IO15
  // These pins do not appear to work from my testing:
  //   IO35, IO36, IO39
  {
    1,                    // eth_address,
    16,                   // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_IN    // eth_clk_mode
  },

  // ESP32-POE
  {
     0,                   // eth_address,
    12,                   // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT  // eth_clk_mode
  },

   // WESP32
  {
    0,			              // eth_address,
    -1,			              // eth_power,
    16,			              // eth_mdc,
    17,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_IN	  // eth_clk_mode
  },

  // QuinLed-ESP32-Ethernet
  {
    0,			              // eth_address,
    5,			              // eth_power,
    23,			              // eth_mdc,
    18,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT	// eth_clk_mode
  },

  // TwilightLord-ESP32 Ethernet Shield
  {
    0,			              // eth_address,
    5,			              // eth_power,
    23,			              // eth_mdc,
    18,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT	// eth_clk_mode
  },

  // ESP3DEUXQuattro
  {
    1,                    // eth_address,
    -1,                   // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT  // eth_clk_mode
  },

  // ESP32-ETHERNET-KIT-VE
  {
    1,                    // eth_address,
    5,                    // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_IP101,        // eth_type,
    ETH_CLOCK_GPIO0_IN    // eth_clk_mode
  },

  // QuinLed-Dig-Octa Brainboard-32-8L and LilyGO-T-ETH-POE
  {
    0,			              // eth_address,
    -1,			              // eth_power,
    23,			              // eth_mdc,
    18,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT	// eth_clk_mode
  },

  // ABC! WLED Controller V43 + Ethernet Shield & compatible
  {
    1,                    // eth_address, 
    5,                    // eth_power, 
    23,                   // eth_mdc, 
    33,                   // eth_mdio, 
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT	// eth_clk_mode
  },

  // Serg74-ESP32 Ethernet Shield
  {
    1,                    // eth_address,
    5,                    // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT  // eth_clk_mode
  },

  // ESP32-POE-WROVER
  {
    0,                    // eth_address,
    12,                   // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_OUT   // eth_clk_mode
  },
  
  // LILYGO T-POE Pro
  // https://github.com/Xinyuan-LilyGO/LilyGO-T-ETH-Series/blob/master/schematic/T-POE-PRO.pdf
  {
    0,			              // eth_address,
    5,			              // eth_power,
    23,			              // eth_mdc,
    18,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_OUT	// eth_clk_mode
  },

 // Gledopto Series With Ethernet
 {
    1,                    // eth_address,
    5,                    // eth_power,
    23,                   // eth_mdc,
    33,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_IN	 // eth_clk_mode
  },
 
 // WLED_ETH_QUINLED_V4 (14) - QuinLED Dig-Uno/Quad v4
  {
    0,                   // eth_address
    -1,                  // eth_power
    7,                   // eth_mdc
    8,                   // eth_mdio
    ETH_PHY_LAN8720,     // eth_type
    ETH_CLOCK_GPIO0_IN   // eth_clk_mode
  },
 
 // WLED_ETH_QUINLED_OCTA_V4 (15) - QuinLED Dig-Octa 32-8L v4
  {
    0,                   // eth_address
    -1,                  // eth_power
    23,                  // eth_mdc
    18,                  // eth_mdio
    ETH_PHY_LAN8720,     // eth_type
    ETH_CLOCK_GPIO0_IN   // eth_clk_mode
  },
};

// sanity checks for ethernet config table and WLED_ETH_DEFAULT
static_assert((sizeof(ethernetBoards)/sizeof(ethernetBoards[0])) == (WLED_NUM_ETH_TYPES - WLED_NUM_ETH_SPI_TYPES), "ethernetBoards[] must describe every non-SPI ethernet type.");

#elif CONFIG_IDF_TARGET_ESP32P4
// ToDO: add P4 specific ethernet options
#error "Ethernet is not yet supported on your ESP32-P4".

#elif !defined(WLED_ETH_HAS_SPI)
#error "Ethernet is not yet supported on your MCU".
#endif

#ifdef WLED_ETH_DEFAULT
  static_assert(((WLED_ETH_DEFAULT) >= WLED_ETH_NONE) && ((WLED_ETH_DEFAULT) < WLED_NUM_ETH_TYPES), "WLED_ETH_DEFAULT is out of range.");
#endif

// Assign an RFC 3927 link-local (APIPA) address to the Ethernet interface.
//
// This exists because a netif cannot hold a static address while the DHCP client
// is probing -- starting the client zeroes the address -- so on a segment with no
// DHCP server the interface would sit at 0.0.0.0 for the full lease timeout. lwIP's
// own AUTOIP would handle this properly, but CONFIG_LWIP_AUTOIP is not enabled in the
// stock prebuilt libraries, so derive the address here instead. On a framework built
// with it, WLED_ETH_LWIP_AUTOIP is set and this path is not used.
//
// The host part comes from the MAC so the address is stable across reboots and
// unique between boards on the same segment.
void applyEthernetLinkLocal()
{
  // Read from efuse rather than ETH.macAddress(): this runs from initEthernet(),
  // which is called during config load, before the interface has started and can
  // report its own MAC. ESP_MAC_ETH is what the driver programs into the W5500
  // anyway, so the result is identical and available much earlier.
  uint8_t mac[6] = {0};
  if (esp_read_mac(mac, ESP_MAC_ETH) != ESP_OK) WLEDNetwork.localMAC(mac);
  uint8_t third  = mac[4];
  uint8_t fourth = mac[5];
  if (third  == 0 || third  == 255) third  = 1; // 169.254.0.x and 169.254.255.x are reserved
  if (fourth == 0 || fourth == 255) fourth = 1; // avoid network and broadcast addresses

  // No gateway: an RFC 3927 link-local segment has no router by definition, and
  // inventing one only makes off-subnet traffic ARP for a host that is not there.
  // Nothing reads it either -- the broadcast address is derived from the interface
  // address and mask, not the gateway.
  IPAddress ip(169, 254, third, fourth);
  ETH.config(ip, INADDR_NONE, IPAddress(255, 255, 0, 0));
  ethernetLinkLocalAssigned = true;

  DEBUG_PRINT(F("initE: link-local IP: ")); DEBUG_PRINTLN(ip);
}

// Addressing state, private to this file.
//   IDLE    - link down, or nothing applied yet
//   PROBING - DHCP client running, waiting for a lease before falling back
//   SETTLED - we have an address (leased, static or link-local); nothing left to do
enum EthAddrState : uint8_t { ETH_ADDR_IDLE, ETH_ADDR_PROBING, ETH_ADDR_SETTLED };
static EthAddrState ethAddrState = ETH_ADDR_IDLE;
static unsigned long ethDhcpStartedAt = 0;

// True when lwIP itself can do RFC 3927, which only happens in a framework built with
// CONFIG_LWIP_AUTOIP. ESP-IDF's lwipopts.h then also hardcodes LWIP_DHCP_AUTOIP_COOP,
// so the DHCP client keeps discovering while a link-local address is held and hands
// over cleanly when a lease arrives -- strictly better than the fallback below, which
// has to stop DHCP to install an address. Detected, not assumed: the same source
// builds correctly either way.
#if defined(CONFIG_LWIP_AUTOIP)
  #define WLED_ETH_LWIP_AUTOIP 1
#else
  #define WLED_ETH_LWIP_AUTOIP 0
#endif

static void startEthernetDhcp()
{
  DEBUG_PRINTLN(F("initE: starting DHCP"));
  ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  ethernetLinkLocalAssigned = false;
  ethDhcpStartedAt = millis();
  // With lwIP AUTOIP there is nothing for us to time out: lwIP self-assigns after
  // CONFIG_LWIP_AUTOIP_TRIES discovers and keeps the DHCP client running regardless.
  ethAddrState = WLED_ETH_LWIP_AUTOIP ? ETH_ADDR_SETTLED : ETH_ADDR_PROBING;
}

// Give the Ethernet interface its address. Called from initEthernet() at boot, from
// handleEthernetAddressing() when the link comes up, and when the addressing settings
// change. Deliberately not called from the ETH_CONNECTED event: config() is not
// idempotent, so a second caller would restart the DHCP client.
void applyEthernetConfig()
{
  // A dedicated Ethernet static IP takes priority over everything else.
  // NB: compare through uint32_t, never against a literal 0. IPAddress on
  // arduino-esp32 3.x dropped operator==(uint32_t); a zero constant then binds to
  // operator==(const uint8_t*), which memcmp()s from address 0 and panics. It
  // compiles silently -- this crashed every boot before it was caught.
  if (uint32_t(ethernetStaticIP) != 0U) {
    DEBUG_PRINTLN(F("initE: using Ethernet static IP"));
    ETH.config(ethernetStaticIP, ethernetStaticGW, ethernetStaticSN, dnsAddress);
    ethernetLinkLocalAssigned = false;
    ethAddrState = ETH_ADDR_SETTLED;
    return;
  }

  // Backwards compatibility: WiFi slot 0's static IP used to be shared with Ethernet.
  // See https://github.com/wled/WLED/issues/5247
  if (multiWiFi.size() && multiWiFi[0].staticIP != IPAddress() && multiWiFi[0].staticGW != IPAddress()) {
    DEBUG_PRINTLN(F("initE: using WiFi slot 0 static IP"));
    ETH.config(multiWiFi[0].staticIP, multiWiFi[0].staticGW, multiWiFi[0].staticSN, dnsAddress);
    ethernetLinkLocalAssigned = false;
    ethAddrState = ETH_ADDR_SETTLED;
    return;
  }

  // No static address: ask for DHCP. On a framework built with CONFIG_LWIP_AUTOIP, lwIP
  // self-assigns an RFC 3927 address after CONFIG_LWIP_AUTOIP_TRIES discovers and keeps
  // the DHCP client running, handing over cleanly if a server later answers. Without it,
  // handleEthernetAddressing() does a cruder version of the same thing.
  startEthernetDhcp();
}

// Drives the DHCP -> link-local fallback, and doubles as a safety net for a missed
// ETH_CONNECTED event: initEthernet() runs from deserializeConfig(), well before
// WiFi.onEvent() is registered in setup(), so on a cold boot with the cable already
// connected the link can come up and the event fire with nothing listening. esp_netif
// still brings the netif up, so everything looks healthy while the interface has no
// address at all.
//
// Must be cheap and must NOT re-apply configuration on every call. NetworkInterface::
// config() is not idempotent -- with INADDR_NONE it does dhcpc_stop() then
// dhcpc_start(), a full restart that resets lwIP's discover counter. Calling it in a
// loop would keep DHCP permanently in its first attempt and, on an AUTOIP build, stop
// lwIP ever reaching CONFIG_LWIP_AUTOIP_TRIES.
void handleEthernetAddressing()
{
  if (!ETH.linkUp()) {
    ethAddrState = ETH_ADDR_IDLE;   // re-apply when the link returns
    return;
  }

  if (ethAddrState == ETH_ADDR_IDLE) {
    DEBUG_PRINTLN(F("initE: link up without configuration, applying"));
    applyEthernetConfig();
    return;
  }

  if (ethAddrState != ETH_ADDR_PROBING) return;

  if (ETH.localIP()[0] != 0) {
    DEBUG_PRINT(F("initE: DHCP lease: ")); DEBUG_PRINTLN(ETH.localIP());
    ethAddrState = ETH_ADDR_SETTLED;
    return;
  }

  if (millis() - ethDhcpStartedAt > WLED_ETH_DHCP_TIMEOUT) {
    DEBUG_PRINTLN(F("initE: no DHCP lease, falling back to link-local"));
    applyEthernetLinkLocal();       // stops the DHCP client as a side effect
    ethAddrState = ETH_ADDR_SETTLED;
  }
}

bool initEthernet()
{
  static bool successfullyConfiguredEthernet = false;

  if (successfullyConfiguredEthernet) {
    // DEBUG_PRINTLN(F("initE: ETH already successfully configured, ignoring"));
    return false;
  }
  if (ethernetType == WLED_ETH_NONE) {
    return false;
  }
  if (ethernetType >= WLED_NUM_ETH_TYPES) {
    DEBUG_PRINTF_P(PSTR("initE: Ignoring attempt for invalid ethernetType (%d)\n"), ethernetType);
    return false;
  }

  DEBUG_PRINTF_P(PSTR("initE: Attempting ETH config: %d\n"), ethernetType);

#ifdef WLED_ETH_HAS_SPI
  if (isSpiEthernet(ethernetType)) {
    if (!PinManager::allocateMultiplePins(esp32_spi_ethernet_pins, WLED_ETH_SPI_PINS_COUNT, PinOwner::Ethernet)) {
      DEBUG_PRINTLN(F("initE: Failed to allocate SPI ethernet pins"));
      return false;
    }
    if (!ETH.begin((eth_phy_type_t)ETH_PHY_W5500, (int32_t)WLED_ETH_SPI_ADDR,
                   WLED_ETH_SPI_CS, WLED_ETH_SPI_IRQ, WLED_ETH_SPI_RST,
                   (spi_host_device_t)WLED_ETH_SPI_HOST,
                   WLED_ETH_SPI_SCLK, WLED_ETH_SPI_MISO, WLED_ETH_SPI_MOSI,
                   WLED_ETH_SPI_FREQ_MHZ)) {
      DEBUG_PRINTLN(F("initE: ETH.begin() failed for SPI PHY"));
      for (managed_pin_type mpt : esp32_spi_ethernet_pins) {
        PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
      }
      return false;
    }
    applyEthernetConfig();
    successfullyConfiguredEthernet = true;
    DEBUG_PRINTLN(F("initE: *** SPI Ethernet successfully configured! ***"));
    return true;
  }
#endif

#ifdef WLED_ETH_HAS_RMII

  // Ethernet initialization should only succeed once -- else reboot required
  ethernet_settings es = ethernetBoards[ethernetType];
  managed_pin_type pinsToAllocate[10] = {
    // first six pins are non-configurable
    esp32_nonconfigurable_ethernet_pins[0],
    esp32_nonconfigurable_ethernet_pins[1],
    esp32_nonconfigurable_ethernet_pins[2],
    esp32_nonconfigurable_ethernet_pins[3],
    esp32_nonconfigurable_ethernet_pins[4],
    esp32_nonconfigurable_ethernet_pins[5],
    { (int8_t)es.eth_mdc,   true },  // [6] = MDC  is output and mandatory
    { (int8_t)es.eth_mdio,  true },  // [7] = MDIO is bidirectional and mandatory
    { (int8_t)es.eth_power, true },  // [8] = optional pin, not all boards use
    { ((int8_t)0xFE),       false }, // [9] = replaced with eth_clk_mode, mandatory
  };
  // update the clock pin....
  if (es.eth_clk_mode == ETH_CLOCK_GPIO0_IN) {
    pinsToAllocate[9].pin = 0;
    pinsToAllocate[9].isOutput = false;
  } else if (es.eth_clk_mode == ETH_CLOCK_GPIO0_OUT) {
    pinsToAllocate[9].pin = 0;
    pinsToAllocate[9].isOutput = true;
  } else if (es.eth_clk_mode == ETH_CLOCK_GPIO16_OUT) {
    pinsToAllocate[9].pin = 16;
    pinsToAllocate[9].isOutput = true;
  } else if (es.eth_clk_mode == ETH_CLOCK_GPIO17_OUT) {
    pinsToAllocate[9].pin = 17;
    pinsToAllocate[9].isOutput = true;
  } else {
    DEBUG_PRINTF_P(PSTR("initE: Failing due to invalid eth_clk_mode (%d)\n"), es.eth_clk_mode);
    return false;
  }

  if (!PinManager::allocateMultiplePins(pinsToAllocate, 10, PinOwner::Ethernet)) {
    DEBUG_PRINTLN(F("initE: Failed to allocate ethernet pins"));
    return false;
  }

  /*
  For LAN8720 the most correct way is to perform clean reset each time before init
  applying LOW to power or nRST pin for at least 100 us (please refer to datasheet, page 59)
  ESP_IDF > V4 implements it (150 us, lan87xx_reset_hw(esp_eth_phy_t *phy) function in 
  /components/esp_eth/src/esp_eth_phy_lan87xx.c, line 280)
  but ESP_IDF < V4 does not. Lets do it:
  [not always needed, might be relevant in some EMI situations at startup and for hot resets]
  */
  #if ESP_IDF_VERSION_MAJOR==3
  if(es.eth_power>0 && es.eth_type==ETH_PHY_LAN8720) {
    pinMode(es.eth_power, OUTPUT);
    digitalWrite(es.eth_power, 0);
    delayMicroseconds(150);
    digitalWrite(es.eth_power, 1);
    delayMicroseconds(10);
  }
  #endif

#if defined(ESP_IDF_VERSION) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
  if (!ETH.begin(    // parameter order in V5 has changed
                (eth_phy_type_t)   es.eth_type,
                (int32_t) es.eth_address,
                (int)     es.eth_mdc,
                (int)     es.eth_mdio,
                (int)     es.eth_power,
                (eth_clock_mode_t) es.eth_clk_mode
                )) {
#else
  if (!ETH.begin(
                (uint8_t) es.eth_address,
                (int)     es.eth_power,
                (int)     es.eth_mdc,
                (int)     es.eth_mdio,
                (eth_phy_type_t)   es.eth_type,
                (eth_clock_mode_t) es.eth_clk_mode
                )) {
#endif
    DEBUG_PRINTLN(F("initE: ETH.begin() failed"));
    // de-allocate the allocated pins
    for (managed_pin_type mpt : pinsToAllocate) {
      PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    }
    return false;
  }

  applyEthernetConfig();

  successfullyConfiguredEthernet = true;
  DEBUG_PRINTLN(F("initE: *** Ethernet successfully configured! ***"));
  return true;
#endif // WLED_ETH_HAS_RMII

  DEBUG_PRINTLN(F("initE: no driver for this ethernet type"));
  return false;
}
#endif


//by https://github.com/tzapu/WiFiManager/blob/master/WiFiManager.cpp
int getSignalQuality(int rssi)
{
    int quality = 0;

    if (rssi <= -100)
    {
        quality = 0;
    }
    else if (rssi >= -50)
    {
        quality = 100;
    }
    else
    {
        quality = 2 * (rssi + 100);
    }
    return quality;
}


void fillMAC2Str(char *str, const uint8_t *mac) {
  sprintf_P(str, PSTR("%02x%02x%02x%02x%02x%02x"), MAC2STR(mac));
  byte nul = 0;
  for (int i = 0; i < 6; i++) nul |= *mac++;  // do we have 0
  if (!nul) str[0] = '\0';                    // empty string
}

void fillStr2MAC(uint8_t *mac, const char *str) {
  for (int i = 0; i < 6; i++) *mac++ = 0;     // clear
  if (!str) return;                           // null string
  uint64_t MAC = strtoull(str, nullptr, 16);
  for (int i = 0; i < 6; i++) { *--mac = MAC & 0xFF; MAC >>= 8; }
}


// performs asynchronous scan for available networks (which may take couple of seconds to finish)
// returns configured WiFi ID with the strongest signal (or default if no configured networks available)
int findWiFi(bool doScan) {
  if (multiWiFi.size() <= 1) {
    DEBUG_PRINTF_P(PSTR("WiFi: Default SSID (%s) used.\n"), multiWiFi[0].clientSSID);
    return 0;
  }

  int status = WiFi.scanComplete(); // complete scan may take as much as several seconds (usually <6s with not very crowded air)

  if (doScan || status == WIFI_SCAN_FAILED) {
    DEBUG_PRINTF_P(PSTR("WiFi: Scan started. @ %lus\n"), millis()/1000);
    WiFi.scanNetworks(true);  // start scanning in asynchronous mode (will delete old scan)
  } else if (status >= 0) {   // status contains number of found networks (including duplicate SSIDs with different BSSID)
    DEBUG_PRINTF_P(PSTR("WiFi: Found %d SSIDs. @ %lus\n"), status, millis()/1000);
    int rssi = -9999;
    size_t selected = (static_cast<size_t>(selectedWiFi) < multiWiFi.size()) ? static_cast<size_t>(selectedWiFi) : 0; // ensure valid starting index
    for (int o = 0; o < status; o++) {
      DEBUG_PRINTF_P(PSTR(" SSID: %s (BSSID: %s) RSSI: %ddB\n"), WiFi.SSID(o).c_str(), WiFi.BSSIDstr(o).c_str(), WiFi.RSSI(o));
      for (unsigned n = 0; n < multiWiFi.size(); n++)
        if (!strcmp(WiFi.SSID(o).c_str(), multiWiFi[n].clientSSID)) {
          bool foundBSSID = memcmp(multiWiFi[n].bssid, WiFi.BSSID(o), 6) == 0;
          // find the WiFi with the strongest signal (but keep priority of entry if signal difference is not big)
          if (foundBSSID || (n < selected && WiFi.RSSI(o) > rssi-10) || WiFi.RSSI(o) > rssi) {
            rssi = foundBSSID ? 0 : WiFi.RSSI(o); // RSSI is only ever negative
            selected = n;
          }
          break;
        }
    }
    DEBUG_PRINTF_P(PSTR("WiFi: Selected SSID: %s RSSI: %ddB\n"), multiWiFi[selected].clientSSID, rssi);
    return selected;
  }
  //DEBUG_PRINT(F("WiFi scan running."));
  return status; // scan is still running or there was an error
}


bool isWiFiConfigured() {
  return multiWiFi.size() > 1 || (strlen(multiWiFi[0].clientSSID) >= 1 && strcmp_P(multiWiFi[0].clientSSID, PSTR(DEFAULT_CLIENT_SSID)) != 0);
}

#if defined(ESP8266)
  #define ARDUINO_EVENT_WIFI_AP_STADISCONNECTED WIFI_EVENT_SOFTAPMODE_STADISCONNECTED
  #define ARDUINO_EVENT_WIFI_AP_STACONNECTED    WIFI_EVENT_SOFTAPMODE_STACONNECTED
  #define ARDUINO_EVENT_WIFI_STA_GOT_IP         WIFI_EVENT_STAMODE_GOT_IP
  #define ARDUINO_EVENT_WIFI_STA_CONNECTED      WIFI_EVENT_STAMODE_CONNECTED
  #define ARDUINO_EVENT_WIFI_STA_DISCONNECTED   WIFI_EVENT_STAMODE_DISCONNECTED
#elif defined(ARDUINO_ARCH_ESP32) && !defined(ESP_ARDUINO_VERSION_MAJOR) //ESP_IDF_VERSION_MAJOR==3
  // not strictly IDF v3 but Arduino core related
  #define ARDUINO_EVENT_WIFI_AP_STADISCONNECTED SYSTEM_EVENT_AP_STADISCONNECTED
  #define ARDUINO_EVENT_WIFI_AP_STACONNECTED    SYSTEM_EVENT_AP_STACONNECTED
  #define ARDUINO_EVENT_WIFI_STA_GOT_IP         SYSTEM_EVENT_STA_GOT_IP
  #define ARDUINO_EVENT_WIFI_STA_CONNECTED      SYSTEM_EVENT_STA_CONNECTED
  #define ARDUINO_EVENT_WIFI_STA_DISCONNECTED   SYSTEM_EVENT_STA_DISCONNECTED
  #define ARDUINO_EVENT_WIFI_AP_START           SYSTEM_EVENT_AP_START
  #define ARDUINO_EVENT_WIFI_AP_STOP            SYSTEM_EVENT_AP_STOP
  #define ARDUINO_EVENT_WIFI_SCAN_DONE          SYSTEM_EVENT_SCAN_DONE
  #define ARDUINO_EVENT_ETH_START               SYSTEM_EVENT_ETH_START
  #define ARDUINO_EVENT_ETH_CONNECTED           SYSTEM_EVENT_ETH_CONNECTED
  #define ARDUINO_EVENT_ETH_DISCONNECTED        SYSTEM_EVENT_ETH_DISCONNECTED
#endif

#if defined(ARDUINO_ARCH_ESP32) && defined(LWIP_IPV6)
#include "lwip/raw.h"
#include "lwip/icmp6.h"
// This is a terrible workaround for a terrible bug: on ESP32 platforms, unsolicited IPv6 router
// advertisements will cause LwIP to overwrite the IPv4 DNS servers with the IPv6 DNS servers
// mentioned in the RA packet.  As a workaround, we just blackhole those packets using the raw
// callback, since we don't yet support IPv6.
//
// This may have been improved in IDF v5 -- Espressif has added a feature to store DNS servers
// on a per interface basis in their LwIP fork.
//
// References:
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/lwip.html (see the "Note" block under "Adapted APIs" -- though it very much undersells the problem.)
// https://github.com/espressif/arduino-esp32/discussions/9988 - links to older discussions
static u8_t blockRouterAdvertisements(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
  // ICMPv6 type is the first byte of the payload, so we skip the header
  if (p->len > 0 && (pbuf_get_at(p, sizeof(struct ip6_hdr)) == ICMP6_TYPE_RA)) {
    pbuf_free(p);
    return 1; // claim the packet — lwIP will not pass it further
  }
  return 0; // not consumed, pass it on
}

void installIPv6RABlocker() {
  struct raw_pcb* ra_blocker = raw_new_ip_type(IPADDR_TYPE_V6, IP6_NEXTH_ICMP6);
  raw_recv(ra_blocker, blockRouterAdvertisements, NULL);
}
#endif

// Runtime state private to this file - previously WLED_GLOBAL, a leftover from
// when all state lived in one big extern block regardless of who used it.
static byte apClients = 0;

//handle Ethernet connection event
void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      // AP client disconnected
      if (apClients > 0) apClients--;
      if (apClients == 0 && isWiFiConfigured()) forceReconnect = true; // no clients reconnect WiFi if available
      DEBUG_PRINTF_P(PSTR("WiFi-E: AP Client Disconnected (%d) @ %lus.\n"), (int)apClients, millis()/1000);
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      // AP client connected
      apClients++;
      DEBUG_PRINTF_P(PSTR("WiFi-E: AP Client Connected (%d) @ %lus.\n"), (int)apClients, millis()/1000);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      DEBUG_PRINT(F("WiFi-E: WiFi IP address: ")); DEBUG_PRINTLN(WLEDNetwork.wifiIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      // followed by IDLE and SCAN_DONE
      DEBUG_PRINTF_P(PSTR("WiFi-E: Connected! @ %lus\n"), millis()/1000);
      wasConnected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (wasConnected && interfacesInited) {
        DEBUG_PRINTF_P(PSTR("WiFi-E: Disconnected! @ %lus\n"), millis()/1000);
        if (interfacesInited && multiWiFi.size() > 1 && WiFi.scanComplete() >= 0) {
          findWiFi(true); // reinit WiFi scan
        }
        // Reconnect on the next loop rather than waiting out the timer in
        // handleConnection(). Gated on wasConnected above, so a failing
        // authentication cannot turn this into a hot loop.
        forceReconnect = true;
        interfacesInited = false;
      }
      break;
  #ifdef ARDUINO_ARCH_ESP32
    case ARDUINO_EVENT_WIFI_READY:
      DEBUG_PRINTLN(F("WiFi-E: driver ready."));
      break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      // also triggered when connected to selected SSID
      DEBUG_PRINTLN(F("WiFi-E: SSID scan completed."));
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      DEBUG_PRINTLN(F("WiFi-E: STA Started"));
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      DEBUG_PRINTLN(F("WiFi-E: STA Stopped"));
      break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
      DEBUG_PRINTLN(F("WiFi-E: STA authentication mode change."));
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      DEBUG_PRINTLN(F("WiFi-E: IP address lost."));
      break;

    case ARDUINO_EVENT_WIFI_AP_START:
      DEBUG_PRINTLN(F("WiFi-E: AP Started"));
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      DEBUG_PRINTLN(F("WiFi-E: AP Stopped"));
      break;
    #if defined(WLED_USE_ETHERNET)
    case ARDUINO_EVENT_ETH_START:
      DEBUG_PRINTLN(F("ETH-E: Started"));
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      {
      DEBUG_PRINTLN(F("ETH-E: Connected"));
      // WiFi is deliberately left running: both interfaces serve traffic
      // simultaneously on this build. See WLEDNetwork's per-interface accessors.
      //
      // Addressing is deliberately NOT applied here. handleEthernetAddressing() owns it
      // and runs every loop, so it has usually already applied configuration by the time
      // this event arrives. Applying again would not be harmless: config() with
      // INADDR_NONE does dhcpc_stop() then dhcpc_start(), restarting the DHCP client and
      // resetting lwIP's discover counter, which delays AUTOIP by a full retry.
      char hostname[64] = {'\0'}; // any "hostname" within a Fully Qualified Domain Name (FQDN) must not exceed 63 characters
      getWLEDhostname(hostname, sizeof(hostname), true); // create DNS name based on mDNS name if set, or fall back to standard WLED server name
      ETH.setHostname(hostname);
      showWelcomePage = false;
      break;
      }
    case ARDUINO_EVENT_ETH_GOT_IP:
      DEBUG_PRINT(F("ETH-E: IP address: ")); DEBUG_PRINTLN(WLEDNetwork.ethernetIP());
      ethernetLinkLocalAssigned = (ETH.localIP()[0] == 169 && ETH.localIP()[1] == 254);
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      DEBUG_PRINTLN(F("ETH-E: Disconnected"));
      ethernetLinkLocalAssigned = false; // re-applied on the next ETH_CONNECTED
      // This doesn't really affect ethernet per se,
      // as it's only configured once.  Rather, it
      // may be necessary to reconnect the WiFi when
      // ethernet disconnects, as a way to provide
      // alternative access to the device.
      if (interfacesInited && WiFi.scanComplete() >= 0) findWiFi(true); // reinit WiFi scan
      forceReconnect = true;
      break;
    #endif
  #endif
    default:
      DEBUG_PRINTF_P(PSTR("WiFi-E: Event %d\n"), (int)event);
      break;
  }
}

