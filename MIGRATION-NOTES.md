# W5500 Ethernet: ESP-IDF 5 migration notes

Decision log for rebasing the W5500 + dual-interface work from
`w5500-ethernet-attempt1` onto current upstream WLED. Written for review after the
fact — if you disagree with a call here, this is the list to argue with.

## Why a rewrite and not a merge

`attempt1` forked at `cc5b5047` (2025-12-03, `0.16.0-alpha`). Upstream had moved
**1060 commits** ahead by the time of this migration, shipped **v16.0.0**, and was on
`17.0.0-devV5`. A trial merge produced 15 conflicted files / 28 hunks that resolved
cleanly and then would not have compiled, because three upstream changes break the
old code without ever showing up as a textual conflict:

1. **ESP32 builds moved from ESP-IDF 4.4 / arduino-esp32 2.0.9 to IDF 5.x / arduino-esp32 3.x.**
2. **`Network` → `WLEDNetwork`** (class and global both renamed to avoid a core clash).
3. **A sweeping "encapsulate globals as static" refactor** (~12 PRs) across exactly the
   files this work touches: `network.cpp`, `e131.cpp`, `util.cpp`, `set.cpp`,
   `wled_server.cpp`, `wled.h`.

So: fresh branch off `upstream/main`, design carried over, code re-derived.
`attempt1` is frozen and tagged (`attempt1-base`, `attempt1-working`).

## The big win: both library forks are gone

`attempt1` depended on two personal forks, and **neither is needed any more**:

| Fork | Why it existed | Status |
|---|---|---|
| [`ETHClass2`](https://github.com/coliniuliano/ETHClass2) | IDF 4.4's stock `ETH` had no SPI PHY support | **Deleted.** arduino-esp32 3.x has native W5500 support in `ETH` |
| [`TroyHacksAsyncUDP`](https://github.com/coliniuliano/TroyHacksAsyncUDP) | Backported IDF 5.1 `AsyncUDP` fixes (incl. a working `localIP()`) into 4.4 | **Deleted.** The core `AsyncUDP` on IDF5 has them |

Verified against the installed framework before writing any code:

- `ETH.begin(eth_phy_type_t, int32_t phy_addr, int cs, int irq, int rst, spi_host_device_t, int sck, int miso, int mosi, uint8_t freq_mhz)` exists and is **not** gated behind `ETH_SPI_SUPPORTS_CUSTOM`.
- `CONFIG_ETH_USE_SPI_ETHERNET=y` and `CONFIG_ETH_SPI_ETHERNET_W5500=y` in the prebuilt `esp32s3/sdkconfig`.
- `CONFIG_ETH_USE_ESP32_EMAC` is **absent** on S3, confirming the RMII path must be compiled out there.
- After building, `esp_eth_phy_new_w5500` and the `w5500_*` MAC driver symbols are present in `firmware.map`.

The `espressif32 @ 5.4.0` platform pin is gone; the env now inherits `esp32s3`, so it
tracks whatever IDF5 platform upstream uses.

## Design decisions

### RMII vs SPI is a compile-time capability, not a target check

Upstream gated all Ethernet on `#if CONFIG_IDF_TARGET_ESP32` and `#error`ed otherwise,
which is why Ethernet could not build on an S3 at all. That is now two flags in
`wled_ethernet.h`:

- `WLED_ETH_HAS_RMII` — needs the EMAC peripheral, classic ESP32 only
- `WLED_ETH_HAS_SPI` — needs no EMAC, works on any variant, requires arduino-esp32 3.x

`network.cpp` only `#error`s when *neither* is available.

**Why the whole `ethernet_settings` struct is guarded, not just one field:**
`eth_clock_mode_t` is itself only declared under `CONFIG_ETH_USE_ESP32_EMAC`. `attempt1`
wrapped just the `eth_clk_mode` member in `#ifndef CONFIG_IDF_TARGET_ESP32S3`; that
happened to work but was treating a symptom. The type does not exist on an S3, so the
struct that uses it cannot be declared there either.

### W5500 is ethernet type 16, numbered past the end of `ethernetBoards[]`

`ethernetBoards[]` is an RMII-only table and SPI PHYs have no sensible entry in it
(different pins entirely). Rather than pad the table with a dummy row, SPI types are
numbered last and `isSpiEthernet()` gates every access. Upstream's `static_assert` on
the table size now reads `WLED_NUM_ETH_TYPES - WLED_NUM_ETH_SPI_TYPES`, so it still
catches a mismatch.

This cost a UI renumber: the dropdown option is `16`, not `13` as in `attempt1` —
upstream has since added types 13–15 (Gledopto, QuinLED v4 Uno/Quad, QuinLED v4 Octa).
**Any existing device config that stored `eth.type: 13` now means Gledopto, not W5500.**
Re-select the board in the settings UI after flashing.

### SPI pins are build flags with board defaults, not hardcoded constants

`attempt1` hardcoded the pins as `#define ETH_MISO_PIN 13` etc. They are now
`WLED_ETH_SPI_MISO` and friends, `#ifndef`-guarded in `wled_ethernet.h`, so another
board can override them from `platformio.ini` without touching source. Defaults are
unchanged from `attempt1` (MISO 13, MOSI 11, SCLK 12, CS 10, IRQ 9, RST 8, addr 1,
`SPI3_HOST`).

They stay compile-time rather than runtime because `initEthernet()` is called from
`deserializeConfig()`, so the pins must be known before config exists.

### Link-local is a runtime setting, and DHCP remains the default

> **Superseded on `w5500-ethernet-autoip`.** The `WLED_ETH_LINK_LOCAL_FALLBACK` compile flag
> described below became the runtime `ethernetAddrMode` setting (DHCP only / DHCP then
> link-local / link-local only, default DHCP only). The priority chain is unchanged. If that
> branch is merged back, this section is superseded by `AUTOIP.md`.

`attempt1` removed the DHCP path outright. That is correct for a lighting segment with
no DHCP server but would regress every RMII user on a normal network, so
`applyEthernetConfig()` now resolves in priority order:

1. dedicated `ethernetStaticIP` if set
2. WiFi slot 0's static IP (upstream's existing behaviour, [#5247](https://github.com/wled/WLED/issues/5247)) — kept for backwards compatibility
3. `WLED_ETH_LINK_LOCAL_FALLBACK` set → RFC 3927 link-local; otherwise → DHCP

The `esp32s3_w5500` env sets the flag. Every other env keeps upstream behaviour exactly.

The link-local host part is derived from the MAC, so it is stable across reboots and
unique between boards. The underlying reason it is needed at all: a netif cannot hold a
static address while the DHCP client probes (starting the client zeroes the address), so
without this the interface sits at `0.0.0.0` for the full lease timeout. lwIP's AUTOIP
would do this properly, but `CONFIG_LWIP_AUTOIP` is not enabled in the prebuilt libs.

Investigated properly on `w5500-ethernet-autoip`: AUTOIP **would** work if the libraries had
it — an AUTOIP-assigned address reaches `IP_EVENT_ETH_GOT_IP` through exactly the same path
as a DHCP one, because esp_netif keys that event on a generic IPv4 address-change callback
rather than on DHCP state. Enabling it needs a rebuilt framework (one Kconfig line, but a
private framework fork and a standing maintenance cost). The WLED code there detects
`CONFIG_LWIP_AUTOIP` at compile time and defers to lwIP when present, so the same source is
correct either way.

### Dropped from `attempt1`

- **`util.cpp` `chip_info.full_revision` shim.** Was an IDF4 back-compat guard. IDF5 has
  the field; the shim is dead weight.
- **`boards/lolin_s3_mini.json` edits.** Unrelated modification to an upstream board
  definition (ldscript, USB mode, variant, psram_type). Not needed for W5500 and not
  this branch's business.
- **`readme-original.md`.** Only existed because `attempt1` overwrote `readme.md`.
  Upstream's readme is intact here.
- **The espalexa dual-response change** — see Known limitations below. This one I think
  was actively wrong.
- **`Network.cpp`'s "`ETH.localIP()` returns 0.0.0.0 but static IP is configured"
  fallbacks.** These papered over an `ETHClass2`/IDF4 quirk where the driver did not
  report its own configured address. The native driver manages the netif properly, so
  the accessors now trust `ETH.localIP()`. **Verified on hardware** — the native driver
  reports its configured address correctly, so the fallback is not needed.

### Verbose debug output: trimmed, not dropped

`attempt1` grew a ~60-line network dump in `loop()` while chasing the PoE livelock. It
is now ~15 lines behind `WLED_DEBUG`: per-interface IP, link speed/duplex, a
`(link-local)` marker, RSSI, and WiFi status + AP state on failure. Enough to diagnose a
headless board without burning flash on a state-machine trace that has served its
purpose.

## The IDF5 trap that actually bit

Worth recording in full: this is exactly the class of breakage that makes a textual merge
dangerous, and it cost a boot loop.

`attempt1` wrote `ethernetStaticIP != (uint32_t)0x00000000`. On arduino-esp32 **2.x**,
`IPAddress` had `operator==(uint32_t)` and that did the obvious thing. On **3.x**,
`IPAddress` gained IPv6 and that overload is **gone**. What remains is:

```cpp
bool operator==(const IPAddress &addr) const;
bool operator==(const uint8_t *addr) const;   // memcmp(addr, ..., 4)
```

The zero constant binds to the `const uint8_t*` overload as a null pointer, and
`memcmp(nullptr, ..., 4)` panics with `LoadProhibited` / `EXCVADDR: 0x00000000`. It
compiles with no warning, and because `initEthernet()` runs from `deserializeConfig()` the
panic lands before anything useful boots — so it presents as a bare reboot loop with no
obvious cause.

**Rule: never compare an `IPAddress` against a literal zero.** Use `uint32_t(addr) != 0U`
or compare against `IPAddress()`. Upstream already uses both idioms; `attempt1`'s was the
IDF4-era one. `INADDR_NONE` is safe either way, since `0xffffffff` is not a null pointer
constant.

A related ordering bug was fixed alongside it: `applyEthernetLinkLocal()` derived its host
part from `ETH.macAddress()`, which needs the interface to have started. It now reads
`esp_read_mac(mac, ESP_MAC_ETH)` from efuse — the same value the driver programs into the
W5500, available long before the netif exists.

## Fixes carried forward

Three real bug fixes from the `attempt1` debugging, kept because they are independent of
the driver swap:

- **`Serial.setTxTimeoutMs(0)`** — HWCDC raises its TX timeout from 0 to 100ms the first
  time it sees a USB connection and never lowers it. On a PoE-powered board, floating
  D+/D- can trip that detection with nothing draining the buffer; with `WLED_DEBUG` the
  "Loop delayed" message then re-triggers itself every iteration and the main loop
  livelocks — no web server, no sync, and `handleSerial()` never sees a restart command
  either. This was the "poe-only race condition" in `attempt1`.
- **`WiFi.setAutoReconnect(false)`** — WLED drives reconnection from
  `handleConnection()`; leaving the core's retry loop on means both fight over the radio
  and the soft-AP beacon gets starved while the core retunes.
- **Unconditional `forceReconnect` on STA disconnect** — upstream only sets it when
  more than one network is configured, so a single-network device waits out the full
  reconnect timer. Gated on `wasConnected`, so a failing authentication cannot hot-loop.

Plus two smaller ones: `169.254.0.0/16` added to the trusted-subnet list (link-local
clients were getting "Access Denied" on the PIN check), and a `~` serial command that
restarts the device — a recovery path on a board with no buttons.

## One upstream bug fixed in passing

`notify()` and `sendSysInfoUDP()` computed their broadcast address as
`~subnetMask | gatewayIP`. On a segment with no router the gateway is legitimately
`0.0.0.0`, which yields a bogus broadcast address. Now `directedBroadcast()` derives it
from the interface address instead: `~mask | (ip & mask)`. This affects upstream too,
not just this branch.

While there, `sendSysInfoUDP()`'s 40-line packet build was factored into
`sendSysInfoUDPForIP()` rather than duplicated per interface the way `attempt1` did it
(it had two near-identical copies, including a `#ifdef` chain for the node type ID that
upstream has since replaced with `uint8_t(WLED_BOARD)`).

## Known limitations

- **Alexa/SSDP discovery only works on the primary interface.** `espalexa` calls
  `beginMulticast(WLEDNetwork.localIP(), ...)` once at init, so it joins the multicast
  group on one interface only. `attempt1` "fixed" this by sending two SSDP responses
  with different IPs out the single socket, which advertises an unreachable address to
  roughly half of requesters — worse than the limitation. Doing this properly needs two
  sockets. Alexa is disabled in the `esp32s3_w5500` env anyway.
- **WebSocket-delivered DMX uses the primary IP** for Art-Net poll replies.
  `AsyncWebSocket` does not expose which interface a frame arrived on. UDP-delivered
  E1.31/Art-Net is interface-correct, which is the path that matters.
- **mDNS** advertises on whichever interfaces the core registers; not explicitly
  controlled here.

## Build status

| Environment | Target | Result |
|---|---|---|
| `esp32s3_w5500` | S3 + W5500 SPI, link-local | builds |
| `esp32_eth` | classic ESP32 + RMII (regression) | builds |
| `esp32s3dev_8MB_qspi` | S3, no Ethernet (regression) | builds |
| `esp32dev` | classic ESP32, no Ethernet (regression) | builds |
| `nodemcuv2` | ESP8266 (regression) | builds |

The env sets `-D WLED_DEBUG`, which is required for the checklist below: it enables the
`DEBUG_PRINT*` output *and* the 2.5s startup delay that lets the USB host enumerate before
WLED starts printing. Without it you get silence on both counts, because
`Serial.setTxTimeoutMs(0)` drops output rather than blocking on it. Comment the flag out
for a release build.

**Hardware status: Ethernet confirmed working** on an ESP32-S3 SuperMini + W5500,
2026-10-09:

```
initE: *** SPI Ethernet successfully configured! ***
ETH-E: Connected
Connected! IP address: 169.254.70.125 via Ethernet
  ETH:  169.254.70.125  100Mbps FD (link-local)
```

Confirmed: checklist items **1 and 2 only**. The link-local address is now derived from
efuse, so it is deterministic by construction rather than merely observed stable.

Everything else is still open. In particular **item 6 (PoE-only boot) is NOT tested** --
that boot was over USB, which is the exact condition the HWCDC livelock fix exists to
survive, so it remains unexercised. Nothing above the link layer has been tested at all:
no web UI, no Art-Net, no LED output, no second interface.

## Testing checklist

1. Boots, USB serial shows `initE: *** SPI Ethernet successfully configured! ***`.
2. `initE: link-local IP: 169.254.x.y` with a stable x.y across reboots.
3. Web UI reachable at that address from a laptop on the same dumb switch.
4. **No "Access Denied"** on the settings pages (the trusted-subnet fix).
5. Cold boot with the cable already connected — the `ETH: link up with no address`
   safety net should fire if the event was missed, and the device should still get an
   address.
6. PoE-only boot, no USB: device comes up and stays responsive (the HWCDC livelock fix).
7. Set a static Ethernet IP in the UI — applies without a reboot; changing the Ethernet
   *type* still reboots.
8. `/json/info` reports `eth_ip`, and `wifi_ip` too once WiFi is also connected.
9. Art-Net poll from a controller on the Ethernet segment — reply carries the Ethernet
   IP, not the WiFi one.
10. Unplug/replug the cable — address is re-applied, `ethernetLinkLocalAssigned` resets.

## If you want to upstream the W5500 support

The RMII/SPI split and the native driver support are genuinely useful to upstream and
have no equivalent there. To extract them, take commit `Add W5500 SPI Ethernet support
using the native IDF5 driver` and drop:

- the `ethernetStaticIP`/`GW`/`SN` globals and their `cfg.cpp` / `set.cpp` / `xml.cpp` /
  `settings_wifi.htm` plumbing
- `WLED_ETH_LINK_LOCAL_FALLBACK` and `applyEthernetLinkLocal()`
- the `WLEDNetwork` per-interface accessors

What is left is the capability flags, the SPI settings block, the `initEthernet()` SPI
branch, the `isSpiEthernet()` guards, and the build env — a self-contained feature.
The simultaneous-WiFi-and-Ethernet commit is invasive and should stay local.
