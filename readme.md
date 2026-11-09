# WLED fork — W5500 Ethernet for DMX lighting

ESP32-S3 + W5500 SPI Ethernet, for driving LEDs over sACN/E1.31/Art-Net on a lighting
segment that has no DHCP server, while WiFi stays available for the web UI.

Upstream WLED's readme is at **[readme-upstream.md](readme-upstream.md)**. Everything below
is about this fork.

## Branches

Everything lives on **`w5500-ethernet-espidf5`** — that is the one running branch, and the
only one to work on. `main` is kept clean, tracking upstream WLED, so upstream can be
pulled; no fork work goes there. Earlier branches have been deleted.

## What changed, and why

**Ethernet on the ESP32-S3 at all.** Upstream WLED only speaks RMII, and the S3 has no RMII
peripheral, so a wired S3 was not possible. This drives a W5500 — an SPI Ethernet chip —
through the native SPI PHY driver in arduino-esp32 3.x, with no external library. *In the
UI:* a "W5500 SPI module" entry in the Ethernet type dropdown under WiFi settings.

**Ethernet has its own IP settings**, separate from the WiFi slots. A lighting segment and a
house network are different places: Ethernet wants a fixed address on the DMX side while
WiFi still takes DHCP from the router, and a single shared IP config forced a choice between
them. *In the UI:* a new "Ethernet IP" block with static IP, gateway and subnet fields.

**An address even when nothing hands one out.** A dumb switch in a rig has no DHCP server,
and WLED used to leave the interface at `0.0.0.0` indefinitely — unreachable, with no way in
to fix it. It now self-assigns an RFC 3927 link-local address (`169.254.x.y`) derived from
the Ethernet MAC, so it is the same every boot and predictable from the MAC, and it still
accepts a DHCP lease if a server ever turns up.

**A custom-built framework, to get lwIP's AUTOIP.** Self-assigning by hand has a flaw: the
DHCP client has to be stopped to hold an address, so a server appearing later goes unnoticed.
lwIP already solves this properly — RFC 3927 is built into ESP-IDF, and it self-assigns after
a couple of unanswered discovers while leaving DHCP running, so a late lease still wins. It
simply isn't switched on; no stock Arduino framework sets `CONFIG_LWIP_AUTOIP=y`. Since the
capability is already there, `tools/autoip-build/` builds a framework with it enabled rather
than reimplementing it worse. The firmware checks at compile time and steps out of lwIP's way
when lwIP can do the job, so the same source is correct either way and none of this is needed
for a normal build.

**One patch to ESP-IDF, for a bug that custom build exposed.** `rmt_tx_wait_all_done()` logs
an error even when polled with a zero timeout, which is exactly how NeoPixelBus checks
whether an LED frame has finished — so the console filled at ~40 lines a second. The stock
framework hides it by disabling IDF logging wholesale; this build keeps logging and fixes the
cause. Upstream bug [espressif/esp-idf#17527](https://github.com/espressif/esp-idf/issues/17527),
still open.

**WiFi and Ethernet both up at the same time.** Previously one global notion of "the"
interface meant the web UI and the lighting protocols had to share whichever won. Now there
are per-interface accessors, and UDP sync, node discovery and Art-Net poll replies answer on
whichever interface the request arrived on — so the rig can be reachable from a laptop on
WiFi while Art-Net flows over the wire.

**SoC temperature in the Info panel.** Cheap to read and useful in an enclosure that has no
other instrumentation. Uses the Arduino core's `temperatureRead()`, which already owns the
single `temperature_sensor` driver instance, and is guarded so non-S3 targets still build.
*In the UI:* a "SoC temperature" row, in °C.

**`net_debug` builds on Ethernet.** Its host lookup called `ETH.hostByName()`, which does not
exist on `ETHClass` in arduino-esp32 3.x, so the Ethernet branch had never compiled — nothing
ships with `WLED_DEBUG_HOST` defined, so nobody had hit it. It now uses
`NetworkManager::hostByName()`, correct for both interfaces because lwIP's DNS is not
interface-specific.

**Project plumbing.** A board definition for the ESP32-S3 SuperMini (`boards/s3_supermini.json`);
a 1.9MB app partition for the autoip environment, because its framework produces a ~495kB
larger binary; two standalone diagnostic sketches (`w5500-spi-test/` for the SPI link,
`wifi-scan-test/` for isolating a radio fault from firmware); and the ESP exception decoder
in the recommended VS Code extensions.

Pin assignment is compile-time and overridable via `WLED_ETH_SPI_*` build flags; defaults
match the ESP32-S3 SuperMini wiring. See `wled00/wled_ethernet.h`.

## Build environments

| env | framework | use when |
|---|---|---|
| `esp32s3_w5500` | stock (tasmota, as upstream WLED ships) | **default.** Hardware-verified |
| `esp32s3_w5500_autoip` | custom-built with `CONFIG_LWIP_AUTOIP=y` | when you want lwIP to do RFC 3927 itself |

```bash
pio run -e esp32s3_w5500 --target upload
```

The `_autoip` env needs its framework built first — see
[tools/autoip-build/README.md](tools/autoip-build/README.md). It will not build without it.

### Serviceability without USB

A PoE-powered board in an enclosure has no serial port, so the two flags that replace it are
**defaults in `platformio.ini`** on both W5500 environments, not local overrides — you cannot
work on this board without them.

- **`WLED_ENABLE_AOTA`** compiles in ArduinoOTA, so `pio run -t upload` flashes over the
  network. Without it the code is `#ifdef`'d out and the settings checkbox is hidden
  entirely. The runtime `aota` setting must also be on, and only takes effect at boot:

  ```bash
  curl -X POST -H "Content-Type: application/json" -d '{"ota":{"aota":true}}' http://<ip>/json/cfg
  ```

- **`WLED_DEBUG_HOST`** sends every `DEBUG_PRINT` to UDP instead of Serial — `DEBUGOUT` is
  one or the other, never both, so defining it means USB stays silent even when connected.
  Receive with `nc -l -u 7868`. It defaults to the limited broadcast address
  `255.255.255.255` so it works for whoever is on the segment without baking in a
  developer's IP, and survives the host changing address or interface. lwIP does not gate
  broadcast sends on `SO_BROADCAST` here (`IP_SOF_BROADCAST` defaults to 0 and ESP-IDF does
  not override it). Point it at one host in `platformio_override.ini` if you would rather not
  flood the broadcast domain.

What stays local, in gitignored `platformio_override.ini`, is only the per-deployment upload
target:

```ini
[env:esp32s3_w5500_autoip]
upload_protocol = espota
upload_port = 169.254.174.98
upload_flags = --auth=wledota
```

Bootstrapping a board that does not yet have ArduinoOTA needs one flash by another route.
WLED's own HTTP endpoint does it over Ethernet:

```bash
curl -F "update=@.pio/build/esp32s3_w5500_autoip/firmware.bin" http://169.254.174.98/update
```

## Status

**Verified on hardware** (ESP32-S3 rev v0.2, 4MB flash, 2MB quad PSRAM, 29 SK6812 RGBW on
GPIO 1):

- `esp32s3_w5500` and `esp32s3_w5500_autoip` both boot with Ethernet up at 100Mbps full
  duplex. The address is deterministic: lwIP's AUTOIP seeds from the Ethernet MAC, so the
  same board always lands on the same `169.254.x.y`.
- Which code path assigned the address is visible in the address itself — lwIP's derivation
  and WLED's fallback produce different hosts from the same MAC.
- OTA over Ethernet: HTTP `/update` takes ~6 s for a 1.7MB image; espota ~35 s.
- `WLED_DEBUG_HOST` logging over Ethernet.
- `socTemp` reports plausible values (52.4 °C idle).

**Not verified:** DHCP handover (needs a switch with a DHCP server), Art-Net over Ethernet
under load, simultaneous WiFi + Ethernet — the WiFi interface has been down in recent
testing while Ethernet carried everything.

**Open issue — intermittent panic.** The board reboots occasionally with WLED's error 90
(`ERR_SYS_REBOOT`) and `resetReason0/1 = 12` (`RTC_SW_CPU_RESET`). That combination means
the panic handler ran: `errorFlag` is only set to 90 under `ResetReason::Crash`, which needs
`esp_reset_reason()` to be `PANIC` or a watchdog. It is **not** a brownout — that is error 91
and RTC reason 15 — so PoE power delivery is not implicated. No backtrace yet: the board has
no USB, and reading the coredump partition over the network needs firmware support that is
not written (see below).

## Notes for this rig

**Use a static Ethernet IP if startup time matters.** It is applied directly on link-up —
no DHCP wait, no RFC 3927 conflict probing, same address every power cycle. Link-local
takes ~5–9 s, most of which is mandatory ARP probing.

**`esp32s3_w5500_autoip` uses a 1.9MB app partition** (vs 1.5MB), because its framework
produces a ~495kB larger binary. It sits at 87.9% full, or 88.9% with ArduinoOTA and
`net_debug` added. OTA still works — the layout keeps two app slots — but the filesystem
drops from 1MB to 256kB, and switching between the two environments reformats it.

**A crash dump is waiting in flash and there is no way to read it.** `big_partitions`
reserves a 64K `coredump` partition and the framework has
`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` with the ELF format, so panics are recorded. But
`espota` and `/update` are write-only and `/edit` only sees the filesystem, so extracting it
needs either USB or ~30 lines of firmware exposing `esp_core_dump_image_get()` over HTTP.

**Keep the ELF if you want to decode a dump.** `esp_coredump` needs the exact
`firmware.elf` that was running when the panic happened, and PlatformIO wipes every build
directory whenever the project config changes — adding `platformio_override.ini` is enough
to do it. Copy `firmware.elf` somewhere safe before changing build config.

**`WLED_DEBUG` is on** in both environments. Turn it off for a deployment: it costs flash,
and on a PoE-only board the USB CDC TX path can stall the main loop (see
`Serial.setTxTimeoutMs(0)` in `wled00/wled.cpp`). Note that defining `WLED_DEBUG_HOST`
moves this output off Serial entirely — `DEBUGOUT` is one or the other, not both.

## Documentation

| file | what |
|---|---|
| [AUTOIP.md](AUTOIP.md) | link-local addressing: design decisions, what lwIP does vs what WLED does, timings |
| [MIGRATION-NOTES.md](MIGRATION-NOTES.md) | historical decision log for the ESP-IDF 4→5 rewrite: why each call was made, what was dropped. Background only |
| [tools/autoip-build/](tools/autoip-build/) | building the custom framework, and tearing it down afterwards |
