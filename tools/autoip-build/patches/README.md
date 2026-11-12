# Patches

Applied to the cloned sources before `build.sh`. Each one exists because of an open
upstream bug; **delete the file once the upstream fix lands** and the next clone picks it
up on its own.

Nothing here is a fork. The patches live in this repo, are applied at build time, and the
clones themselves stay on upstream refs.

| patch | upstream | why |
|---|---|---|
| `esp-idf/0001-rmt-no-flush-timeout-log-on-zero-wait.diff` | [espressif/esp-idf#17527](https://github.com/espressif/esp-idf/issues/17527) (open) | `rmt_tx_wait_all_done()` logs `ESP_LOGE "flush timeout"` even when called with a 0 timeout, which is a legitimate non-blocking "is it done yet?" poll. NeoPixelBus polls exactly that way once per LED frame (`NeoEsp32RmtXMethod.h:95`), so the console fills at ~40 lines/second. The legacy `rmt_wait_tx_done()` suppressed the log when the wait was 0; this restores that. A maintainer confirmed the approach in Sept 2025 but it has not landed as of IDF 5.5.5. |
| `mdns/0001-negative-aaaa-response-nsec.diff` | none filed (espressif/mdns 1.15.1) | With no IPv6 address, the board answered an IPv6 (AAAA) query for its own name with nothing, so a dual-stack lookup of `<name>.local` waits out the resolver's timeout: 5 s per lookup in macOS `getaddrinfo()` on any Mac with a routable IPv6 address (Tailscale gives every Mac one). The patch sends the negative answer RFC 6762 §6.1 asks for, an NSEC record saying the name has only an A record. Measured: `dns-sd -G v6` now reports "No Such Record", and `getaddrinfo(AF_UNSPEC)` went from 5.0 s to under 10 ms. **Applied differently, see below.** |

## Applying

```bash
podman run --rm --security-opt label=disable \
  -v wled-autoip-work:/work -v "$PWD/tools/autoip-build/patches":/patches:ro \
  wled-autoip-builder bash -lc '
    cd /work/lib-builder/esp-idf
    for p in /patches/esp-idf/*.diff; do
      git apply --check "$p" 2>/dev/null && git apply "$p" && echo "applied $(basename $p)" \
        || echo "skipped $(basename $p) (already applied or no longer needed)"
    done'
```

`git apply --check` first makes this idempotent and makes a stale patch visible rather than
fatal: if it no longer applies, that usually means upstream fixed it and the file should go.

### The mDNS patch goes in after packaging

`espressif/mdns` is a managed component, and `build.sh` exports
`IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS=1`, so a patch applied before `build.sh` is
replaced by a fresh download. `../patch-mdns.sh` instead patches the source in the finished
build tree, rebuilds that one archive with `ninja` (no reconfigure, so nothing else changes:
rebuilt unpatched, it matches the packaged archive byte for byte), and copies it into the
packaged framework, keeping the original as `libespressif__mdns.a.unpatched`. Run it after
`package.sh`, every time the framework is rebuilt:

```bash
podman run --rm --security-opt label=disable \
  -v wled-autoip-work:/work -v wled-autoip-tools:/home/builder/.espressif \
  -v "$PWD/tools/autoip-build":/scripts:ro -v "$HOME/.platformio/custom":/out \
  wled-autoip-builder bash /scripts/patch-mdns.sh esp32s3
```

**Then rebuild the firmware with the build cache off.** `platformio.ini` sets
`build_cache_dir`, and the framework's libraries reach the linker as `-l` flags, so the cache
key does not change when a library does: PlatformIO restores the previously linked firmware
from the cache and the new library never reaches the board. Point the cache somewhere empty
for that one build:

```bash
PLATFORMIO_BUILD_CACHE_DIR="$(mktemp -d)" pio run -e esp32s3_w5500_autoip -t upload
```

Check the result: `xtensa-esp32s3-elf-nm .pio/build/esp32s3_w5500_autoip/firmware.elf | grep no_aaaa`
should list `append_self_no_aaaa`.

## Why not just silence the log

`CONFIG_LOG_DEFAULT_LEVEL=0` hides it, which is what tasmota's framework does and why the
stock WLED build looks clean — the same spurious errors are generated there, just never
printed. That also hides every genuine ESP-IDF error, which is worth keeping during board
bring-up. Per-tag filtering (`esp_log_level_set("rmt", ...)`) cannot help either: the
spurious message and real RMT failures are both `ESP_LOGE` on the same tag.
