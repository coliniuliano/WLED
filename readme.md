# WLED fork — W5500 Ethernet for DMX lighting

ESP32-S3 + W5500 SPI Ethernet, for driving LEDs over sACN/E1.31/Art-Net on a lighting
segment that has no DHCP server, while WiFi stays available for the web UI.

Upstream WLED's readme is at **[readme-upstream.md](readme-upstream.md)**. Everything below
is about this fork.

## Branches

| branch | state |
|---|---|
| `w5500-ethernet-attempt1` | **frozen.** Built against ESP-IDF 4.4 and two personal library forks. Kept as a reference; tagged `attempt1-base` / `attempt1-working` |
| `w5500-ethernet-espidf5` | **working firmware.** Rebased onto current upstream, native IDF5 SPI Ethernet driver, no library forks |
| `w5500-ethernet-autoip` | ← *you are here.* Adds lwIP-level link-local addressing, which needs a custom-built framework |

## Build environments

| env | framework | use when |
|---|---|---|
| `esp32s3_w5500` | stock (tasmota, as upstream WLED ships) | **default.** Hardware-verified |
| `esp32s3_w5500_autoip` | custom-built with `CONFIG_LWIP_AUTOIP=y` | experimental — see Status |

```bash
pio run -e esp32s3_w5500 --target upload
```

The `_autoip` env needs its framework built first — see
[tools/autoip-build/README.md](tools/autoip-build/README.md). It will not build without it.

## What this fork adds

- **W5500 SPI Ethernet on ESP32-S3.** Upstream only supports RMII, which the S3 has no
  hardware for. Uses the native SPI PHY driver in arduino-esp32 3.x — no external library.
- **A dedicated Ethernet IP config**, separate from the WiFi slots, so Ethernet can sit on
  a static or DHCP-less segment while WiFi uses DHCP elsewhere.
- **An address when there is no DHCP server.** Previously the interface sat at `0.0.0.0`
  forever. It now ends up with an RFC 3927 `169.254.x.y` address, and still takes a DHCP
  lease whenever one is offered.
- **Simultaneous WiFi and Ethernet** — per-interface accessors, and UDP sync, node
  discovery and Art-Net poll replies answer on the interface a request arrived on.

Pin assignment is compile-time and overridable via `WLED_ETH_SPI_*` build flags; defaults
match the ESP32-S3 SuperMini wiring. See `wled00/wled_ethernet.h`.

## Status

**`esp32s3_w5500` — works on hardware.**

```
initE: *** SPI Ethernet successfully configured! ***
ETH-E: Connected
Connected! IP address: 169.254.70.125 via Ethernet
  ETH:  169.254.70.125  100Mbps FD (link-local)
```

**`esp32s3_w5500_autoip` — boots, but has an open regression.** Ethernet initialises
correctly and the lwIP AUTOIP path is active (`initE: starting DHCP` rather than WLED
self-assigning). However the RMT LED driver logs `rmt_tx_wait_all_done: flush timeout`
once per frame, meaning LED frames are submitted and never complete. The stock framework
on the same board shows zero of these, so it is introduced by the custom framework.
Under investigation — the suspects are the IDF/Arduino version jump (5.5.5 / 3.3.12 vs
5.5.4 / 3.3.8) versus the sdkconfig this fork sets.

**Not yet verified on hardware:** link-local assignment by lwIP, DHCP handover, Art-Net
over Ethernet, simultaneous WiFi + Ethernet.

## Notes for this rig

**Use a static Ethernet IP if startup time matters.** It is applied directly on link-up —
no DHCP wait, no RFC 3927 conflict probing, same address every power cycle. Link-local
takes ~5–9 s, most of which is mandatory ARP probing. AUTOIP is for when you do not want
to configure anything; a fixed installation does not need it.

**`esp32s3_w5500_autoip` uses a 1.9MB app partition** (vs 1.5MB), because its framework
produces a ~495kB larger binary. OTA still works — the layout keeps two app slots — but
the filesystem drops from 1MB to 256kB, and switching between the two environments
reformats it.

**`WLED_DEBUG` is on** in both environments. Turn it off for a deployment: it costs flash,
and on a PoE-only board the USB CDC TX path can stall the main loop (see
`Serial.setTxTimeoutMs(0)` in `wled00/wled.cpp` for the fix that makes it survivable).

## Documentation

| file | what |
|---|---|
| [AUTOIP.md](AUTOIP.md) | link-local addressing: design decisions, what lwIP does vs what WLED does, timings |
| [MIGRATION-NOTES.md](MIGRATION-NOTES.md) | the ESP-IDF 4→5 rebase off `attempt1`: what changed, what was dropped and why |
| [tools/autoip-build/](tools/autoip-build/) | building the custom framework, and tearing it down afterwards |
