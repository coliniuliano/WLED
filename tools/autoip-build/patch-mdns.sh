#!/usr/bin/env bash
# Rebuild only the mDNS library with patches/mdns applied, and swap it into the packaged
# framework. See patches/README.md for what the patch does and why.
#
# Run INSIDE the build container, after a successful ./build.sh -t <target> and package.sh:
#   podman run --rm --security-opt label=disable \
#     -v wled-autoip-work:/work -v wled-autoip-tools:/home/builder/.espressif \
#     -v "$PWD/tools/autoip-build":/scripts:ro -v "$HOME/.platformio/custom":/out \
#     wled-autoip-builder bash /scripts/patch-mdns.sh esp32s3
#
# Why a separate step: espressif/mdns is a managed component. build.sh exports
# IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS=1, so a patch applied before build.sh is
# silently replaced by a fresh download. Patching the source in the existing build tree and
# rebuilding that one archive needs no reconfigure, so nothing else in the framework changes:
# rebuilding it unpatched reproduces the packaged archive byte for byte.
set -euo pipefail

TARGET="${1:?usage: patch-mdns.sh <target>, e.g. esp32s3}"
LB=/work/lib-builder
COMP="$LB/managed_components/espressif__mdns"
LIB=esp-idf/espressif__mdns/libespressif__mdns.a
DEST="/out/framework-arduinoespressif32-libs-autoip/$TARGET/lib/libespressif__mdns.a"

[ -d "$COMP" ] || { echo "no managed mdns component at $COMP: run build.sh first"; exit 1; }
[ -f "$DEST" ] || { echo "no packaged library at $DEST: run package.sh first"; exit 1; }
grep -q "^version: 1.15.1" "$COMP/idf_component.yml" || echo "warning: mdns is not 1.15.1; the patch was written against 1.15.1"

for p in /scripts/patches/mdns/*.diff; do
  if (cd "$COMP" && git apply --reverse --check "$p" 2>/dev/null); then
    echo "already applied: $(basename "$p")"
  else
    (cd "$COMP" && git apply "$p") && echo "applied: $(basename "$p")"
  fi
done

source "$LB/esp-idf/export.sh" > /dev/null
ninja -C "$LB/build" "$LIB"

[ -f "$DEST.unpatched" ] || cp "$DEST" "$DEST.unpatched"
cp "$LB/build/$LIB" "$DEST"
echo "installed patched $(basename "$DEST") ($(md5sum < "$DEST" | cut -c1-8)); original kept as $(basename "$DEST").unpatched"
