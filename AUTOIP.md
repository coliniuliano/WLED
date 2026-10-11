# Ethernet link-local addressing

## The problem

WLED's Ethernet support assumes either a static IP or a DHCP server. On a segment with
neither — a dumb switch in a lighting rig — the interface sits at `0.0.0.0` forever and the
device is unreachable. There is no way to get an address.

## What was done

Two independent pieces, in two commits.

**1. WLED self-assigns a link-local address.** If no static IP is configured and DHCP goes
unanswered, the Ethernet interface takes an RFC 3927 `169.254.x.y` address. It still takes
a DHCP lease whenever one is offered. This works on the **stock** framework and is the part
that matters; everything below is an optimisation of it.

**2. A framework built with `CONFIG_LWIP_AUTOIP=y`.** lwIP can do RFC 3927 itself, and
better: it self-assigns after `CONFIG_LWIP_AUTOIP_TRIES` unanswered discovers while leaving
the DHCP client running, so a lease arriving later is still picked up — no dark window, no
link bounce needed. No stock ESP32 Arduino framework enables it. `tools/autoip-build/`
builds one that does; `esp32s3_w5500_autoip` builds against it.

WLED detects which case it is in at compile time (`#if defined(CONFIG_LWIP_AUTOIP)`) and
steps out of lwIP's way when lwIP can handle it. The same source is correct either way, so
none of this is required to build WLED normally.

## Decisions, and why

**The address is derived from the Ethernet MAC via `esp_read_mac()`.** Stable across
reboots, unique between boards. Read from efuse rather than `ETH.macAddress()` because
`initEthernet()` runs from `deserializeConfig()`, long before the interface has started and
can report its own MAC.

**A static IP always wins, and WiFi slot 0's static IP is still honoured** (upstream
[#5247](https://github.com/wled/WLED/issues/5247)). Every existing configuration behaves
exactly as before; this only changes what happens in the case that previously had no
answer.

**No user-facing setting.** An earlier draft had a DHCP / DHCP-then-link-local /
link-local-only dropdown. It was removed: there is one correct behaviour, and a setting
whose wrong values are all worse is not a feature. Upstream might disagree and want this
gated — see Limitations.

**The missed-`ETH_CONNECTED` safety net became a state machine.** It previously called
`applyEthernetConfig()` on *every loop iteration* while the link was up and unaddressed.
`NetworkInterface::config()` is not idempotent — with `INADDR_NONE` it does `dhcpc_stop()`
then `dhcpc_start()` — so that kept DHCP pinned to its first attempt forever, and would
have prevented lwIP's AUTOIP from ever reaching its try threshold. It also sat below
`handleConnection()`'s early returns, so a WiFi scan could stall Ethernet addressing
entirely; Ethernet does not depend on WiFi state and now runs ahead of those guards.

**Nothing stops AUTOIP when a lease arrives, and that is correct.** The lwIP that ESP-IDF
5.5 pins calls `autoip_start()` from `dhcp.c` but never `autoip_stop()`. That looks like a
bug — it is not. The `autoip_coop_state` machine was deliberately removed upstream in lwIP
commit `7d1c26cc` ("ACD module added + update and improve DHCP + AUTOIP behavior") and
replaced: `netif.c` calls `acd_netif_ip_addr_changed()` on every address change, which
detects link-local → routable and calls `acd_put_in_passive_mode()`. In
`ACD_STATE_PASSIVE_ONGOING` a conflict raises `ACD_DECLINE`, not `ACD_RESTART_CLIENT`, and
`ACD_DECLINE` never calls `netif_set_addr()`. The lease cannot be overwritten. The new
design is also better than the old one: AUTOIP is demoted rather than destroyed, preserving
lwIP's deliberate "keep the same link-local address" behaviour if the lease is later lost.
An earlier version of this work carried a WLED-side `autoip_stop()` workaround; it was
removed once the reason for the upstream change was understood. **There is no esp-idf bug
to report.**

## Reproducing the framework

See `tools/autoip-build/README.md`. In outline: a Podman container on native arm64,
pioarduino's lib-builder on `master`, `defconfig.wled` appended to `configs/defconfig.common`,
`build.sh -t esp32s3`, then `package.sh` to assemble two PlatformIO packages.

The two rules that make it work, both learned the hard way:

- **Use `master`**, not `release/v5.5`. The latter is stale against the sources it clones
  unpinned and cannot complete a build today.
- **Never mix** a rebuilt library tree into the stock framework. They must come from one
  build and ship as a matched pair; otherwise ~200 sdkconfig options disagree, including
  `LWIP_IPV6_NUM_ADDRESSES`, which changes `struct netif`'s size.

## Verified

```
CONFIG_LWIP_AUTOIP=y, CONFIG_LWIP_AUTOIP_TRIES=2    in the packaged sdkconfig
8 autoip_* symbols in liblwip.a                      (stock ships autoip.c.obj empty)
autoip_start/stop/bind/supplied_address              linked into firmware.elf
esp32s3_w5500_autoip                                 1,662,584 B, 87.5% of 1.9MB app
```

Builds clean on `esp32s3_w5500`, `esp32_eth`, `esp32s3dev_8MB_qspi`, `esp32dev` and
`nodemcuv2`.

## Not yet verified

**None of this has run on hardware.** The checklist:

- [ ] Dumb switch, no DHCP → a `169.254.x.y` address, assigned by lwIP rather than WLED
- [ ] Switch with DHCP → a lease is taken and no link-local address appears
- [ ] Boot on a dumb switch, then move to a DHCP network → the lease is picked up without
      a reboot, and is never stolen back afterwards
- [ ] Art-Net keeps flowing across that transition

## Limitations

**On a stock framework**, once WLED falls back to link-local it stays there until the link
bounces — it has to stop the DHCP client to install the address, so it cannot keep
discovering. The AUTOIP build does not have this problem, and it is the reason the
framework work was worth doing.

**Upstream would likely want this gated.** Ethernet with no DHCP now self-assigns instead
of sitting at `0.0.0.0`. That is an improvement, but it is a behaviour change, and upstream
may prefer it opt-in.

**~495 kB of flash** versus tasmota's framework, hence the larger app partition. The cause
is picolibc vs newlib, and fixing it means carrying patches against the Arduino core — see
`tools/autoip-build/README.md` under "Size".

## Startup timing

How long before the interface has an address, on a segment with no DHCP server:

| phase | measured | tunable? |
|---|---|---|
| reset → Ethernet configured | 2.9 s | 2.5 s of it is `WLED_DEBUG`'s USB CDC delay |
| → link up detected | 4.9 s | |
| → address assigned | **14.5 s** | |
| **link-up → address** | **9.6 s** | only the DHCP discovers, not the probing |

Measured on an ESP32-S3 SuperMini + W5500 with the cable already connected at reset.

The probing is `PROBE_WAIT 1` + 3 probes at `PROBE_MIN/MAX 1–2 s` + `ANNOUNCE_WAIT 2`, from
`lwip/prot/acd.h`. `autoip_bind()` runs from the `ACD_IP_OK` callback, so the address only
appears once probing completes. There is no Kconfig for those constants; shortening them
means patching lwIP, which would cost this build its "no source patches" property.

`CONFIG_LWIP_AUTOIP_TRIES=1` would shave ~1 s of that. It is left at the IDF default of 2:
the saving is small next to the probing, and lowering it makes a link-local address more
likely to be taken and then replaced on a network that does have DHCP.

**WLED's own fallback was faster because it skipped conflict probing entirely.** It simply
claimed an address. That is fine with one controller on a segment and silently wrong with
two whose MACs collide in the low two octets. The seconds buy that check.

**If startup time actually matters, use a static Ethernet IP.** Measured above, AUTOIP
costs ~9.6 s after link-up. `applyEthernetConfig()`
calls `ETH.config()` directly on link-up: no DHCP wait, no probing, address live
immediately, and deterministic across power cycles. AUTOIP is for when you do not want to
configure anything; a fixed installation with a known address does not need it.

## An unplanned side effect: WPA3 works

Tasmota's core is tuned to fit WLED into 4MB flash, and that constraint drives most of
their sdkconfig — picolibc, trimmed mbedTLS, no logging, and **no WPA3**. Since this
environment uses the 1.9MB app partition, it does not have to make the same trades.

`CONFIG_ESP_WIFI_ENABLE_WPA3_SAE` and `_OWE_STA` are disabled in tasmota's build and
enabled in ours. That is the direct cause of WLED issue
[#5568](https://github.com/wled/WLED/issues/5568), *"esp32 cannot connect to WPA3/PMF
Wi-Fi network on v16 (used to work on v15.4)"*, where the maintainers concluded:

> The tasmota core we are using in 16.0.0 has limitations in the WPA3 / PMF support. It's
> not our code, it's in the framework.

The issue is **still open**, with no fix available for 4MB boards because the official core
that does support WPA3 does not fit. So this framework can join WPA3/PMF networks that a
stock WLED v16 build cannot — not by design, but as a consequence of not inheriting a
size trade-off we do not need to make.

Untested against an actual WPA3 network.
