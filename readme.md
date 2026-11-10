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

**SoC temperature in the Info panel.** The only instrumentation an enclosed board has. The
sensor is installed directly with a 20–100 °C range rather than through the Arduino core's
`temperatureRead()`, whose hardcoded 10–50 °C hint selects a hardware range that ends at
80 °C — right where this board was sitting, so the reading would have saturated exactly when
it mattered. *In the UI:* a "SoC temperature" row, in °C; also `socTemp` in `/json/info` and in
the 30 s debug report.

**The radio is the heat.** Measured in free air, LEDs and Ethernet unplugged, same board, same
spot: 78 °C with the access point on and the WiFi radio never sleeping; 76 °C with the AP
closed; **55 °C once the WiFi radio was allowed to modem-sleep**; and still 55 °C with TX power
cut from 19.5 dBm to 11 dBm on top — transmit duty cycle is ~1 %, so TX power is left at the
default for link margin. A soft-AP must beacon, so ESP-IDF cannot power-save while one is
up — which is why the AP is no longer left on permanently. Modem sleep costs latency on
*incoming* traffic (ping averaged 180 ms against the router's DTIM period; HTTP stayed at
40 ms). That is fine for the web UI, and realtime data arrives over Ethernet on this rig, so
sleep is simply on whenever the AP is closed. The "Disable WiFi sleep" setting still wins if
ticked; it is an ordinary WiFi setting, not a build flag, and the deployed config has sleep
enabled.

**The access point is a rescue path, not a feature.** This hardware is potted. If it ever talks
itself out of every network there is no USB to fall back on, so the rule is simple and has no
exceptions: if there is no WiFi connection, the AP is up; as soon as WiFi connects, it closes.
That includes the few seconds at boot while WiFi comes up, which costs a brief `WLED-AP`
appearance and nothing else. This has to work *with Ethernet up*
too, which upstream never considered: every AP decision sat behind "is any interface
connected", so a PoE-powered board whose WiFi had gone away would never open one, and never
retry WiFi either. `WLED_AP_RESCUE` (a default build flag here) adds that case — with
Ethernet up, WiFi is retried 30 s ×3 then every 5 min, never during a realtime stream and
never while a phone is on the AP — and refuses any AP behaviour setting weaker than "open
when no connection". "Always" is still allowed, at the cost of the heat above. One more
upstream assumption had to go for this to hold: the WiFi disconnect handler treated "any
interface was connected" as "WiFi dropped", so with Ethernet up every failed WiFi attempt
(one every ~2.5 s with the network absent) forced a reconnect, re-ran interface setup and
closed the AP — measured as the AP blinking off every few seconds. It now only reacts to a
WiFi connection that was actually up. Two
upstream behaviours that broke this path are fixed as well: `initConnection()` reset the whole
radio (`WIFI_MODE_NULL`) on every WiFi reconnect attempt, destroying the AP mid-handshake so a phone
could never finish joining; and a stuck DNS lookup cycled WiFi — AP included — every 30 min,
which on a resolver-less Ethernet segment fixes nothing and never stops.

**The panic loop, root-caused.** The board had been rebooting every NTP lookup. Decoded from the
core dump against the matching ELF: `handleNetworkTime → AsyncDNS::query → dns_gethostbyname →
… → udp_new_ip_type`, asserting on lwIP's thread-safety check. Two WLED call sites use raw lwIP
from the Arduino task (`asyncDNS.h`, upstream since January, and multicast sACN's
`igmp_joingroup()`). That is correct under lwIP's default message-passing model, which every
stock build uses, and a data race under core locking — and the custom framework had core
locking *and* the assertion on, because esp_matter Kconfig-`select`s them. The framework is
returned to the default model (`CONFIG_USE_TCPIP_CORE_LOCK_FOR_THREAD_SAFETY=n`, the only gate
a defconfig can reach), and both call sites now take the lock when a build asks for it, so the
option stays open. Locking off is deliberate: under it, any unlocked lwIP call anywhere in the
image bricks a potted board; under message passing the same omission is simply correct.

**Everything over the network.** Firmware: WLED's `/update` and ArduinoOTA. Config: the web UI
and `/json/cfg`. Logs: `DEBUG` output goes to UDP, and the destination is now a runtime setting
(`{"dbg":{"host":"192.168.10.110","port":7868}}` on `/json/cfg`) because the compile-time
broadcast default never leaves the local segment and the person debugging usually is not on
it. Crashes: `GET /coredump` streams the ELF core dump the panic handler leaves in flash
(`esp-coredump info_corefile -t elf -c coredump.elf firmware.elf`), `/coredump?erase` clears
it, and `/json/info` reports `coredump: true` when one is waiting. The WiFi disconnect reason
code is logged too, because the stock event handler says nothing at all for a WiFi
connection that never comes up.

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
| `esp32s3_w5500_autoip` | custom-built with `CONFIG_LWIP_AUTOIP=y` | **what the board runs.** lwIP does RFC 3927 itself; also the only one with a core dump partition |
| `esp32s3_w5500` | stock (tasmota, as upstream WLED ships) | fallback that needs no custom framework; hand-rolled link-local, no `/coredump` |

```bash
pio run -e esp32s3_w5500_autoip --target upload    # espota, target in platformio_override.ini
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
  not override it). Limited broadcast does not cross a router, so from another subnet set the
  destination at runtime instead (it persists in `cfg.json`):

  ```bash
  curl -X POST -H "Content-Type: application/json" -d '{"dbg":{"host":"192.168.10.110","port":7868}}' http://<ip>/json/cfg
  nc -l -u 7868
  ```

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
- OTA over Ethernet (HTTP `/update` ~6 s, espota ~35 s) and over WiFi (espota, by mDNS name).
- Debug logging over UDP to a host on a *different* subnet, set at runtime via `/json/cfg`.
- `/coredump`: 404 with nothing stored, erase works, `coredump` flag in `/json/info`.
- WiFi + AP coexistence on one channel, and the AP surviving reconnect attempts (a phone
  joined `WLED-AP` while WiFi was failing with a wrong key, then restored the credentials).
- **The rescue AP with Ethernet up**, tested over the wire by pointing WiFi at a network that
  does not exist and rebooting: AP up 13 s after boot and never interrupted (0 `AP Stopped`
  in the log), WiFi retried at exactly 30 s and 60 s with the AP kept open, Ethernet
  serving the web UI throughout; then WiFi restored → connected, AP closed, sleep on.
- Boot with Ethernet and WiFi both available: both up (100 Mbps full duplex link-local +
  WiFi), AP closed, radio sleeping.
- The thermal figures above, each held steady for several minutes before being read.
- The lwIP fix at runtime: NTP enabled over WiFi, which runs the exact `dns_gethostbyname()`
  call that used to assert — `NTP IP resolved`, time synced 23 s after boot, five minutes
  watched with no reboot and no core dump.

**Not verified:** DHCP handover (needs a switch with a DHCP server); Art-Net over Ethernet
under load.

## Notes for this rig

**Use a static Ethernet IP if startup time matters.** It is applied directly on link-up —
no DHCP wait, no RFC 3927 conflict probing, same address every power cycle. Link-local
takes ~5–9 s, most of which is mandatory ARP probing.

**Deployed WiFi settings:** AP behaviour "open when no connection", WiFi sleep enabled, TX
power at the default. A config wipe returns sleep to WLED's default (off) — hot but safe;
the AP behaviour floor is compiled in and survives. A further lever, not taken: the CPU runs
at 240 MHz and 160 MHz would shave a few more degrees, but it touches LED timing and 29
pixels do not need it.

**`esp32s3_w5500_autoip` uses a 1.9MB app partition** (vs 1.5MB), because its framework
produces a ~495kB larger binary. It sits at 89% full. OTA still works — the layout keeps two
app slots — but the filesystem drops from 1MB to 256kB, and switching between the two
environments reformats it.

**Keep the ELF if you want to decode a dump.** `esp_coredump` needs the exact
`firmware.elf` that was running when the panic happened, and PlatformIO wipes every build
directory whenever the project config changes — adding `platformio_override.ini` is enough
to do it. Copy `firmware.elf` somewhere safe before changing build config
(`build_output/` is gitignored and is where the running build's ELF is kept).

**`WLED_DEBUG` is on** in both environments. It costs flash, and on a PoE-only board the USB
CDC TX path can stall the main loop (see `Serial.setTxTimeoutMs(0)` in `wled00/wled.cpp`).
Defining `WLED_DEBUG_HOST` moves this output off Serial entirely — `DEBUGOUT` is one or the
other, not both.

## Documentation

| file | what |
|---|---|
| [AUTOIP.md](AUTOIP.md) | link-local addressing: design decisions, what lwIP does vs what WLED does, timings |
| [MIGRATION-NOTES.md](MIGRATION-NOTES.md) | historical decision log for the ESP-IDF 4→5 rewrite: why each call was made, what was dropped. Background only |
| [tools/autoip-build/](tools/autoip-build/) | building the custom framework, and tearing it down afterwards |
