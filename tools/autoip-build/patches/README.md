# Patches

Applied to the cloned sources before `build.sh`. Each one exists because of an open
upstream bug; **delete the file once the upstream fix lands** and the next clone picks it
up on its own.

Nothing here is a fork. The patches live in this repo, are applied at build time, and the
clones themselves stay on upstream refs.

| patch | upstream | why |
|---|---|---|
| `esp-idf/0001-rmt-no-flush-timeout-log-on-zero-wait.diff` | [espressif/esp-idf#17527](https://github.com/espressif/esp-idf/issues/17527) (open) | `rmt_tx_wait_all_done()` logs `ESP_LOGE "flush timeout"` even when called with a 0 timeout, which is a legitimate non-blocking "is it done yet?" poll. NeoPixelBus polls exactly that way once per LED frame (`NeoEsp32RmtXMethod.h:95`), so the console fills at ~40 lines/second. The legacy `rmt_wait_tx_done()` suppressed the log when the wait was 0; this restores that. A maintainer confirmed the approach in Sept 2025 but it has not landed as of IDF 5.5.5. |

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

## Why not just silence the log

`CONFIG_LOG_DEFAULT_LEVEL=0` hides it, which is what tasmota's framework does and why the
stock WLED build looks clean — the same spurious errors are generated there, just never
printed. That also hides every genuine ESP-IDF error, which is worth keeping during board
bring-up. Per-tag filtering (`esp_log_level_set("rmt", ...)`) cannot help either: the
spurious message and real RMT failures are both `ESP_LOGE` on the same tag.
