#ifndef WLED_ETHERNET_H
#define WLED_ETHERNET_H

#include "pin_manager.h"

#ifdef WLED_USE_ETHERNET

// For ESP32, the remaining five pins are at least somewhat configurable.
// eth_address  is in range [0..31], indicates which PHY (MAC?) address should be allocated to the interface
// eth_power    is an output GPIO pin used to enable/disable the ethernet port (and/or external oscillator)
// eth_mdc      is an output GPIO pin used to provide the clock for the management data
// eth_mdio     is an input/output GPIO pin used to transfer management data
// eth_type     is the physical ethernet module's type (ETH_PHY_LAN8720, ETH_PHY_TLK110)
// eth_clk_mode defines the GPIO pin and GPIO mode for the clock signal
//              However, there are really only four configurable options on ESP32:
//              ETH_CLOCK_GPIO0_IN    == External oscillator, clock input  via GPIO0
//              ETH_CLOCK_GPIO0_OUT   == ESP32 provides 50MHz clock output via GPIO0
//              ETH_CLOCK_GPIO16_OUT  == ESP32 provides 50MHz clock output via GPIO16
//              ETH_CLOCK_GPIO17_OUT  == ESP32 provides 50MHz clock output via GPIO17
typedef struct EthernetSettings {
  uint8_t        eth_address;
  int            eth_power;
  int            eth_mdc;
  int            eth_mdio;
  eth_phy_type_t eth_type;
  #ifndef CONFIG_IDF_TARGET_ESP32S3
  eth_clock_mode_t eth_clk_mode;
  #endif
} ethernet_settings;

extern const ethernet_settings ethernetBoards[];

#define WLED_ETH_RSVD_PINS_COUNT 6
extern const managed_pin_type esp32_nonconfigurable_ethernet_pins[WLED_ETH_RSVD_PINS_COUNT];

// W5500 SPI Ethernet, hardcoded for the current ESP32-S3 board. Declared here
// rather than inside initEthernet() so the settings pages can report the pins
// actually in use -- ethernetBoards[] has no W5500 entry on S3 (its contents are
// RMII-only and compiled out), so it must not be indexed by WLED_ETH_W5500.
#if defined(ARDUINO_ARCH_ESP32S3)
  #define ETH_MISO_PIN 13
  #define ETH_MOSI_PIN 11
  #define ETH_SCLK_PIN 12
  #define ETH_CS_PIN   10
  #define ETH_INT_PIN   9
  #define ETH_RST_PIN   8
  #define ETH_ADDR      1

  #define WLED_ETH_W5500_PINS_COUNT 6
  extern const managed_pin_type w5500_ethernet_pins[WLED_ETH_W5500_PINS_COUNT];
#endif
#endif

#endif