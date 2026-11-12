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
than reimplementing it worse. The firmware's own hand-rolled fallback has since been removed
(2026-11-11): link-local addressing is lwIP's job alone, and a stock-framework build gets
DHCP or a static address only. The same source still builds either way, so none of this is needed
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
cut from 19.5 dBm to 11 dBm on top — transmit duty cycle is ~1 %, so power does not show up as
heat (it is lowered anyway, for the reason in the next paragraph). A soft-AP must beacon, so
ESP-IDF cannot power-save while one is up — which is why the AP is no longer left on
permanently. Modem sleep costs latency on
*incoming* traffic (ping averaged 180 ms against the router's DTIM period; HTTP stayed at
40 ms). That is fine for the web UI, and realtime data arrives over Ethernet on this rig, so
sleep is simply on whenever the AP is closed. The "Disable WiFi sleep" setting still wins if
ticked; it is an ordinary WiFi setting, not a build flag, and the deployed config has sleep
enabled.

**Full transmit power stops working once the chip is hot.** The board dropped off WiFi at
22:16 one night and was still off at 08:30, ten hours later, with no reboot in between
(the uptime said so) and nothing wrong with the radio — a scan from the board listed the
router at −61 dBm. Every connection attempt ended the same way: `reason 2 AUTH_EXPIRE` four
seconds after `Connecting`, the router never answering the authentication. The router's own
log showed the board's last association at 22:10 and nothing until the fix. The chip sat at
70 °C the whole time, because a board without WiFi has its AP open and cannot sleep — which
is also why the condition is self-sustaining. Lowering TX power from 19.5 dBm to 11 dBm
through `/json/cfg` connected on the very next attempt, in 3 s; four attempts at full power
had failed in the preceding minutes. The router hears the board fine either way (RSSI −62),
so what fails is the board's own transmission at full power when hot — a distorted or
supply-starved PA, the usual reason a lower setting cures AUTH_EXPIRE on ESP32 boards; which
of the two it is would take a scope. Reproduced the same morning under control: rescue AP
open to heat the chip to 76 °C, then the real network restored at 19.5 dBm — two attempts,
two `AUTH_EXPIRE` at RSSI −64; power set to 11 dBm, next attempt associated in 3 s. Two
changes: the board build now defaults to 11 dBm
(`WLED_TX_POWER_DEFAULT=44` in `platformio.ini`; the WiFi settings page can still change
it), and `wifi_monitor.cpp` (build flag `WLED_ENABLE_AUTO_TX_POWER`) treats that setting as
a ceiling and chooses the level in use itself. It first stepped power down one level per
three authentication timeouts and stayed there, which could leave a board at 2 dBm for a
failure that power had nothing to do with (a wrong password). Since 2026-11-11 the level
comes from three inputs instead: the router's RSSI (the lowest level that should reach the
router at about −71 dBm, which is 11 dBm at RSSI −62), the chip temperature (capped at
11 dBm from 65 °C until it is back under 60 °C), and failures (three consecutive `AUTH_EXPIRE` move it down a level at
a time to the lowest, then up to the ceiling, then round again, so it never settles on a
level that does not connect). Only `AUTH_EXPIRE` counts: it is what the hot board produced,
the router never answering at all, whereas a wrong password fails later as a handshake
timeout (reason 15 or 204, per the ESP-IDF Wi-Fi guide) and leaves the power alone. The
configured value is untouched and a settings save restarts the choice. Signal strength
and temperature are reviewed every 10 s, connected or not. Effective power shows as
`wifi.txPower` in `/json/info`, and every attempt and change is in `/netlog`.
**The RSSI rule and the 65 °C threshold are derived from the two measurements above, not
tested on hardware.**

**The access point that was "open" with nothing on the air — same fault, seen from the
other side.** For those ten hours the firmware reported the AP open and a phone next to the
board saw no `WLED-AP`. Scanned from a Mac (CoreWLAN; `system_profiler` redacts SSIDs
without Location Services) during the reproduction: at 19.5 dBm and 76 °C the AP was
visible in 10 of 11 scans on channel 6 (and 2 of 2 on channel 1) while the WiFi attempts
were hitting a network that does not exist. It vanished the moment the attempts targeted
the real network on channel 11 and failed, and stayed gone through nine scans — `apActive`
true, no AP-stop event from the driver. ESP-IDF moves a soft-AP to the station's channel on
a connection attempt, so the AP was now on channel 11, where nothing the hot board sends at
full power is receivable: the AP's beacons went the way of the authentication frames. One
second after the automatic step to 11 dBm, `WLED-AP` was back in the scan on channel 11 at
−51 dBm, and the next attempt connected. A restart of the soft-AP after each failed attempt
was tried as well and made no visible difference on its own; it was removed. So the one
fix covers both symptoms, and the netlog records the driver's AP state next to the
firmware's should they ever disagree for another reason.

**The access point is a rescue path, not a feature.** This hardware is potted and its real
deployments are venues with no WiFi at all, so "no WiFi → AP open" would mean running hot
by default (an AP cannot modem-sleep). The policy lives in one file, `rescue_network.cpp`,
as four rules applied every loop from the current facts:

1. WiFi up → AP closed, radio sleeps. The normal state, ~55 °C.
2. WiFi lost, or absent at boot → AP open for **5 minutes** — long enough to join and read
   `/netlog` — and for as long as someone is on it.
3. Window over, WiFi still down → AP closed, WiFi retried **every 10 s** (paced from the
   previous attempt's failure, never while a phone is on the AP or realtime data is
   flowing), radio off between attempts. Ethernet is the way in; it is also the power.
4. No Ethernet address and no WiFi → AP stays open, the only way in. On PoE that is a
   bench, not a venue.

`{"wifi":{"ap":true}}` on `/json/state` (WLED's existing key) opens a 5-minute window on
demand (over Ethernet, to hand a phone a path); `{"wifi":{"ap":false}}` closes it. The Info
panel has the same as a button — "Access point: closed [Open 5 min]". "AP always" in the WiFi settings is honoured as a
permanent rule 4; any weaker AP behaviour is refused (`WLED_AP_RESCUE`, a default build flag
here). Every open and close is a netlog entry with its reason. This is the shape ESPHome
(`ap_timeout`), Tasmota's WiFi manager and WLED's own "Temporary" behaviour converge on; the
difference is that a wired path that doubles as power makes "then close it" safe here where
it is not for a battery or wall-wart device. Measured: with the AP closed and WiFi retrying
every 10 s, the die fell from 75 °C to 63 °C in nine minutes and was still falling — the
radio is off between attempts (ESP-IDF's disconnected-state power management), so the
10 s cadence costs nothing worth having. The first run of this policy also found a crash:
with the AP closed, upstream's reconnect path (`WiFi.disconnect(true)`) left *no* WiFi
interface, so the framework deinitialised the whole stack every retry, and the `WIFI_OFF`
event raced the teardown — the core dump (fetched over `/coredump`, decoded against the kept
ELF) showed the debug printer on the event task reading the station netif as it was
destroyed. Policy retries now keep the station interface (`initConnection(false)`), and
the network helpers check status bits before touching a netif. Upstream never considered
the Ethernet-up case:
every AP decision sat behind "is any interface connected", so a PoE-powered board whose WiFi
had gone away would never open one, and never retry WiFi either; those upstream branches
are compiled out in this build. One more upstream assumption had to go for this to hold: the WiFi disconnect handler treated "any
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
it, and `/json/info` reports `coredump: true` when one is waiting. Network history:
`GET /netlog` lists the last 192 WiFi, AP and Ethernet events — every connection attempt,
every disconnect with its reason code and RSSI, every AP open/close as the firmware decided
it *and* as the driver reported it, TX power steps, sleep changes, and the reset reason of
each boot — kept in RTC memory so it survives a software reset, panic or watchdog (not a
power cycle). A retry loop that changes nothing is written three times in full and then
sampled every 30th repetition, so the entry that matters — what happened *before* the
loop started — is not pushed out in minutes (it was, the first time it was needed). The Info panel shows the most recent WiFi drop with its reason and age, and
the WiFi and Ethernet addresses separately. The stock event handler said nothing at all for
a WiFi connection that never came up; the ten-hour outage above was diagnosed from a single
reason code caught live, which is what this replaces.

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
| `esp32s3_w5500` | stock (tasmota, as upstream WLED ships) | fallback that needs no custom framework; DHCP or static address only (no link-local), no `/coredump` |

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

- **`/netlog`** needs no listener at all: it is the board's own record of what the network
  did, readable after the fact over whichever interface still answers.

  ```bash
  curl http://<ip>/netlog           # oldest first; earlier boots included after a soft reset
  curl http://<ip>/netlog?clear
  ```

- **The rescue AP on demand**, when you have the wire and want a phone on the board:

  ```bash
  curl -X POST -H "Content-Type: application/json" -d '{"wifi":{"ap":true}}' http://<ip>/json/state   # WLED-AP for 5 min, longer while in use
  curl -X POST -H "Content-Type: application/json" -d '{"wifi":{"ap":false}}' http://<ip>/json/state
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
- TX power vs. authentication, on the hot board (70 °C): four attempts at 19.5 dBm, each
  `AUTH_EXPIRE`; the first at 11 dBm associated in 3 s and had an IP in 9 s. Half an hour
  later, connected and sleeping, 56 °C. Reproduced twice more under control at 76 °C.
- **The whole recovery chain, unattended** (with the earlier one-way step-down; the
  automatic transmit power that replaced it has not been through this test), on the hot
  board with 19.5 dBm configured:
  attempts at 60 s, 90 s and 210 s each `AUTH_EXPIRE`; `TX power stepped down to 44`
  on the third; `WLED-AP` (moved to channel 11 by the driver, invisible at full power)
  back in the Mac's scan one second later at −51 dBm; next attempt at 330 s associated in
  3 s, IP 8 s later, AP closed, modem sleep on. Every step in `/netlog`.
- **The rescue policy** (`rescue_network.cpp`): boot with WiFi available → AP open at
  3.4 s, closed at 15.6 s when WiFi got its address, sleep on. Boot without WiFi → AP open,
  attempts every 10.0 s, `AP closed (window over)` at 5:03, sleep on, 75 → 63 °C over the
  next nine minutes; `{"wifi":{"ap":true}}` reopened it, `false` closed it; restoring the
  SSID connected on the next attempt. After the crash fix: 38 retries with the AP closed,
  zero `WIFI_OFF` events and zero full teardowns in the debug capture, no core dump,
  reconnected 7 s after the SSID was restored.
- `/netlog` over WiFi and over Ethernet after the OTA that introduced it: boot reason,
  every attempt, the driver's AP restart as WiFi joined channel 11, `AP closed`,
  `modem sleep on`. After the next OTA the log read `current boot #2` with every boot #1
  entry still there, ending in the `ASSOC_LEAVE` of the OTA reboot — RTC persistence works.
  `/json/info` carries `wifi_ip`, `eth_ip`, `wifi.lastReason`, `wifi.lastReasonName`,
  `wifi.lastReasonAge`, and `wifi.txPower` = 44.

**Not verified:** DHCP handover (needs a switch with a DHCP server); Art-Net over Ethernet
under load; whether the channel-11 failure also occurs at full power on a *cool* board (it
connected on channel 11 at full power all the previous day, so presumably not — but it was
not re-measured once the cause was known); the `WIFI_OFF` race fix beyond "did not recur" —
it was a race hit once in ~40 retries, and the fix removes the teardown that opened the
window rather than the window itself, which a 95 s run cannot prove.

## Notes for this rig

**Use a static Ethernet IP if startup time matters.** It is applied directly on link-up —
no DHCP wait, no RFC 3927 conflict probing, same address every power cycle. Link-local
takes ~5–9 s, most of which is mandatory ARP probing.

**Deployed WiFi settings:** AP behaviour "open when no connection", WiFi sleep enabled, TX
power 11 dBm (`txpwr` 44, also the build default). A config wipe returns sleep to WLED's default (off) — hot but safe;
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
