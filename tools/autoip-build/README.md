# Building an ESP32-S3 framework with `CONFIG_LWIP_AUTOIP`

WLED's Ethernet code self-assigns an RFC 3927 link-local address when no DHCP server
answers. lwIP can do that itself, and do it better — holding a link-local address *while*
the DHCP client keeps discovering, then switching cleanly to a lease — but only if the
framework was built with `CONFIG_LWIP_AUTOIP=y`, which no stock ESP32 Arduino framework is.

This directory builds one that is. WLED detects it at compile time (`#if
defined(CONFIG_LWIP_AUTOIP)`) and steps out of lwIP's way; the same source is correct
either way, so none of this is required to build WLED normally.

Design decisions and rationale are in `../../AUTOIP.md`.

## What gets installed

| Thing | Size | Removal |
|---|---|---|
| Homebrew `podman` | ~70 MB | `brew uninstall podman` |
| Podman VM `wled-build` | 28 GB used (80 GB sparse) | `podman machine rm wled-build` |
| Container image | ~310 MB (in VM) | removed with the VM |
| Volumes `wled-autoip-work`, `wled-autoip-tools` | ~13.7 GB (in VM) | removed with the VM |
| **Output: two PlatformIO packages** | **~400 MB** | `rm -rf ~/.platformio/custom/*-autoip` |

Only the last row is needed to *build* WLED. Everything above it exists purely to produce
it and can be deleted once you have it — at the cost of a ~40 minute rebuild if you ever
need to regenerate it.

## Why Podman, and why not the official image

**Podman** rather than Docker Desktop: Apache-2.0, free, no account.

**Plain `ubuntu:22.04` rather than `espressif/esp32-arduino-lib-builder`:** the official
image is amd64-only, so on Apple Silicon it runs under emulation. ESP-IDF publishes
`linux-arm64` builds of every toolchain needed, so a native arm64 container builds at full
speed. Verify with `podman run --rm ubuntu:22.04 uname -m` → `aarch64`.

## Setup

```bash
brew install podman
podman machine init wled-build --cpus 8 --memory 16384 --disk-size 80
podman machine start wled-build

# from the repo root
podman build -t wled-autoip-builder -f tools/autoip-build/Containerfile tools/autoip-build
podman volume create wled-autoip-work     # source + build tree
podman volume create wled-autoip-tools    # $HOME/.espressif -- see gotcha 1
```

Every run below needs **both volumes** and `--security-opt label=disable` (gotcha 3).

## Build

```bash
PV="-v wled-autoip-work:/work -v wled-autoip-tools:/home/builder/.espressif"
SEL="--security-opt label=disable"

# 1. clone the builder (master -- see "Two rules that matter")
podman run --rm $SEL $PV wled-autoip-builder bash -lc '
  cd /work && git clone --filter=blob:none \
    https://github.com/pioarduino/esp32-arduino-lib-builder.git lib-builder &&
  cd lib-builder && git checkout master'

# 2. apply the config, and drop a TinyUSB patch that no longer applies (gotcha 2)
podman run --rm $SEL $PV -v "$PWD/tools/autoip-build":/scripts:ro wled-autoip-builder bash -lc '
  cd /work/lib-builder &&
  cat /scripts/defconfig.wled >> configs/defconfig.common &&
  rm -f patches/tinyusb/0001-host-esp32s2s3-lowspeed-hub.diff'

# 3. apply patches for open upstream bugs (see patches/README.md)
podman run --rm $SEL $PV -v "$PWD/tools/autoip-build/patches":/patches:ro wled-autoip-builder bash -lc '
  cd /work/lib-builder/esp-idf
  for p in /patches/esp-idf/*.diff; do
    git apply --check "$p" 2>/dev/null && git apply "$p" && echo "applied $(basename $p)" \
      || echo "skipped $(basename $p)"
  done'

# 4. build (~20-40 min first time: clones ESP-IDF and installs toolchains)
podman run --rm $SEL $PV wled-autoip-builder bash -lc \
  'cd /work/lib-builder && ./build.sh -t esp32s3'

# 5. assemble the two PlatformIO packages into ~/.platformio/custom
podman run --rm $SEL $PV -v "$HOME/.platformio/custom":/out \
  -v "$PWD/tools/autoip-build":/scripts:ro \
  wled-autoip-builder bash /scripts/package.sh esp32s3
```

Then `pio run -e esp32s3_w5500_autoip`. The environment already points at the packages via
`symlink://${sysenv.HOME}/.platformio/custom/...`, so there is nothing else to configure.

Sanity checks:

```bash
grep -c '^CONFIG_LWIP_AUTOIP=y' ~/.platformio/custom/framework-arduinoespressif32-libs-autoip/esp32s3/sdkconfig   # 1
grep -oE '\bautoip_(start|stop|bind|supplied_address)\b' .pio/build/esp32s3_w5500_autoip/firmware.map | sort -u   # 4 names
```

## Two rules that matter

**Use `master`.** pioarduino's `release/v5.5` is from 2025-07-01 and is stale against the
sources it clones *unpinned* (ESP-IDF, arduino-esp32, TinyUSB). It cannot complete a build
today. `master` (the same commit as Espressif's) is contemporaneous with the current
Arduino core and needs no workarounds beyond gotcha 2 below.

**Never mix framework and libraries from different builds.** They must come from one
`build.sh` run and ship as a matched pair. Dropping a rebuilt `esp32s3/` tree into the
stock framework leaves ~200 conflicting sdkconfig options, including
`LWIP_IPV6_NUM_ADDRESSES`, which changes `struct netif`'s size — two different struct
layouts in one binary.

## Gotchas

**1. Mount a volume at `/home/builder/.espressif`.** The IDF *clone* lives in `/work` and
persists, but `install.sh` puts the toolchains and python venv under `$HOME/.espressif`,
which is container-local and vanishes with `--rm`. Without this you re-download several GB
every run.

**2. One TinyUSB patch is stale.** `patches/tinyusb/0001-host-esp32s2s3-lowspeed-hub.diff`
no longer applies to current TinyUSB master, and `update-components.sh` treats that as
fatal (deliberately — see `patches/tinyusb/README.md`). It is a USB **host**-mode fix;
WLED's S3 build is device-mode CDC only, so deleting it is safe here. A `git apply --3way`
refresh is the proper fix if you need host mode.

**3. SELinux blocks writes into `managed_components/`. Pass `--security-opt label=disable`
on every run.** Symptom:

```
PermissionError: [Errno 13] Permission denied: '.../managed_components/chmorgan__esp-libhelix-mp3'
```

The directory looks fine — `drwxr-xr-x builder builder` — and `touch` inside it still
fails. Podman's VM is Fedora CoreOS with SELinux enforcing, so ownership and mode are a red
herring. (`-v ...:/work:z` to relabel works too.)

**4. `-s` is a trap.** It skips "installing/updating ESP-IDF and all components", but that
step also *exports* the IDF environment, so you get `line 226: idf.py: command not found`.
Re-run without it; once the clone and toolchains exist it is a fast re-check.

**5. `package.sh` must inline the toolchain flags.** lib-builder writes
`@"/work/lib-builder/build/toolchain/cflags"` response-file references into `flags/*` and
`pioarduino-build.py`. Those paths do not exist once the package is on the host. The script
inlines them and fails loudly if any build-tree path survives.

## Size

1,662,584 bytes, 87.5% of the 1.9 MB app partition that `esp32s3_w5500_autoip` selects.
That is ~495 kB more than the same WLED built against tasmota's framework (1,167,635).
**OTA is unaffected** — the layout keeps two app slots — but the filesystem drops from
1 MB to 256 kB.

`defconfig.wled` carries size trims chosen by diffing against tasmota's sdkconfig. They are
worth having but they are not the answer: measured on their own they saved **9,046 bytes**.
The reason is simple in hindsight — the linker already discards unreferenced code, so
turning off a feature nothing was calling does not shrink anything.

### What would actually matter, and why it is blocked

Tasmota sets **`CONFIG_LIBC_PICOLIBC=y`**; pioarduino defaults to newlib. A symbol-level
diff of the two firmwares shows the full newlib `_vfprintf_r` / `_svfprintf_r` /
`__ssvfiscanf_r` / `_dtoa_r` family present only in ours, ~36 kB before counting what it
drags in. picolibc is the single largest difference between the two builds.

Setting it fails:

```
espressif__cbor/tinycbor/src/open_memstream.c: unknown type name 'cookie_io_functions_t'
                                               implicit declaration of 'fopencookie'
```

picolibc does not provide `fopencookie`. tasmota does not hit this because their component
set has no `cbor` — it arrives via `esp_rainmaker` and `esp_insights`, both declared in
**arduino-esp32's own `idf_component.yml`**. Removing them additionally breaks
`cores/esp32/esp32-hal-log-wrapper.c`, which expects `esp_diagnostics` to provide
`__wrap_log_printf`.

So picolibc costs two patches against the Arduino core — which is exactly what tasmota's
fork carries. That is a real and worthwhile piece of work, but it is maintaining a core
fork, not a config change, and it should be a deliberate decision rather than a drive-by.

## Teardown

```bash
# keep the built packages, drop everything used to make them (~28 GB)
podman machine stop wled-build && podman machine rm wled-build
brew uninstall podman

# and the packages themselves, if you no longer want the autoip build
rm -rf ~/.platformio/custom/framework-arduinoespressif32-autoip \
       ~/.platformio/custom/framework-arduinoespressif32-libs-autoip
rm -rf tools/autoip-build
```

Removing the VM does **not** break `esp32s3_w5500_autoip` — the packages under
`~/.platformio/custom` are self-contained. You only need the VM again to rebuild them.
