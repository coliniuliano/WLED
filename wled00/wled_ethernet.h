#ifndef WLED_ETHERNET_H
#define WLED_ETHERNET_H

#include "pin_manager.h"

#ifdef WLED_USE_ETHERNET

// Which attachment methods the current MCU/core combination can actually build.
//
// RMII needs the ESP32 EMAC peripheral, which only the classic ESP32 (and P4) has.
// `eth_clock_mode_t` itself is only declared when CONFIG_ETH_USE_ESP32_EMAC is set,
// so ethernet_settings below cannot even be *declared* on an S3 -- hence the guard
// around the whole struct rather than around individual fields.
//
// SPI-attached PHYs need no EMAC and work on every ESP32 variant, but native support
// for them only landed in arduino-esp32 3.x / ESP-IDF 5.x. Older cores need an
// external library and are deliberately not supported.
#if CONFIG_ETH_USE_ESP32_EMAC && CONFIG_IDF_TARGET_ESP32
  #define WLED_ETH_HAS_RMII
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3) && CONFIG_ETH_SPI_ETHERNET_W5500
  #define WLED_ETH_HAS_SPI
#endif

#ifdef WLED_ETH_HAS_RMII

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
  eth_clock_mode_t eth_clk_mode;
} ethernet_settings;

// Indexed by ethernetType. Describes RMII boards only -- SPI types are numbered
// past the end of this table, so check isSpiEthernet() before indexing it.
extern const ethernet_settings ethernetBoards[];

#define WLED_ETH_RSVD_PINS_COUNT 6
extern const managed_pin_type esp32_nonconfigurable_ethernet_pins[WLED_ETH_RSVD_PINS_COUNT];

#endif // WLED_ETH_HAS_RMII

#ifdef WLED_ETH_HAS_SPI

// SPI-attached PHY wiring. This is a compile-time property of the board rather
// than a runtime setting: the pins must be known before config load, because
// initEthernet() runs from deserializeConfig(). Override any of these from
// platformio.ini to describe a different board. Defaults match the ESP32-S3
// SuperMini + W5500 wiring this was developed against.
#ifndef WLED_ETH_SPI_MISO
  #define WLED_ETH_SPI_MISO 13
#endif
#ifndef WLED_ETH_SPI_MOSI
  #define WLED_ETH_SPI_MOSI 11
#endif
#ifndef WLED_ETH_SPI_SCLK
  #define WLED_ETH_SPI_SCLK 12
#endif
#ifndef WLED_ETH_SPI_CS
  #define WLED_ETH_SPI_CS   10
#endif
#ifndef WLED_ETH_SPI_IRQ
  #define WLED_ETH_SPI_IRQ   9
#endif
#ifndef WLED_ETH_SPI_RST
  #define WLED_ETH_SPI_RST   8
#endif
#ifndef WLED_ETH_SPI_ADDR
  #define WLED_ETH_SPI_ADDR  1
#endif
#ifndef WLED_ETH_SPI_HOST
  #define WLED_ETH_SPI_HOST  SPI3_HOST
#endif
#ifndef WLED_ETH_SPI_FREQ_MHZ
  #define WLED_ETH_SPI_FREQ_MHZ ETH_PHY_SPI_FREQ_MHZ
#endif

#define WLED_ETH_SPI_PINS_COUNT 6
extern const managed_pin_type esp32_spi_ethernet_pins[WLED_ETH_SPI_PINS_COUNT];

#endif // WLED_ETH_HAS_SPI

// True for ethernet types that are SPI-attached rather than RMII. Callers must
// check this before indexing ethernetBoards[], which only describes RMII boards.
inline bool isSpiEthernet(int type) {
#ifdef WLED_ETH_HAS_SPI
  return type == WLED_ETH_W5500;
#else
  return false;
#endif
}

#endif // WLED_USE_ETHERNET

#endif // WLED_ETHERNET_H
