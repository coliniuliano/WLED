#!/usr/bin/env bash
# Assemble lib-builder output into two PlatformIO packages.
#
# Run INSIDE the build container, after a successful ./build.sh -t <target>:
#   podman run --rm --security-opt label=disable \
#     -v wled-autoip-work:/work -v wled-autoip-tools:/home/builder/.espressif \
#     -v "$HOME/.platformio/custom":/out wled-autoip-builder \
#     bash /work/lib-builder/../package.sh esp32s3
#
# Produces, under /out:
#   framework-arduinoespressif32-autoip/        the Arduino core (cores, libraries, variants)
#   framework-arduinoespressif32-libs-autoip/   the precompiled IDF libraries
#
# They are split because arduino-esp32 3.3.x's pioarduino-build.py resolves the libraries
# as a separate PlatformIO package (framework-arduinoespressif32-libs); lib-builder already
# emits the right manifest for it inside out/tools/esp32-arduino-libs/package.json.
set -euo pipefail

TARGET="${1:?usage: package.sh <target>, e.g. esp32s3}"
LB=/work/lib-builder
OUT=/out
FW="$OUT/framework-arduinoespressif32-autoip"
LIBS="$OUT/framework-arduinoespressif32-libs-autoip"

[ -d "$LB/out/tools/esp32-arduino-libs/$TARGET" ] || { echo "no build output for $TARGET"; exit 1; }

echo "==> assembling framework package"
rm -rf "$FW" "$LIBS"
mkdir -p "$FW"
# tar rather than cp -a: the arduino checkout carries a .git with read-only objects that
# cp cannot reproduce onto the host mount, and we do not want it in the package anyway.
( cd "$LB/components/arduino" && tar -cf - \
    --exclude=.git --exclude=.github --exclude=tests --exclude=docs \
    --exclude=idf_component_examples . ) | ( cd "$FW" && tar -xf - )

echo "==> assembling libs package"
mkdir -p "$LIBS"
( cd "$LB/out/tools/esp32-arduino-libs" && tar -cf - . ) | ( cd "$LIBS" && tar -xf - )

# lib-builder writes @response-file references that point at absolute paths inside the
# build tree, which do not exist once the package is moved. Inline them.
echo "==> inlining toolchain response files"
python3 - "$LB/build/toolchain" "$LIBS/$TARGET" <<'PY'
import os, re, sys
tdir, sdk = sys.argv[1], sys.argv[2]
targets = [os.path.join(sdk, "pioarduino-build.py"),
           *(os.path.join(sdk, "flags", f) for f in ("c_flags", "cpp_flags", "S_flags"))]
for name in ("asmflags", "cflags", "cxxflags"):
    path = os.path.join(tdir, name)
    if not os.path.isfile(path):
        print(f"   ! missing {path}"); continue
    flags = open(path).read().split()
    for t in targets:
        if not os.path.isfile(t): continue
        s = open(t).read()
        if t.endswith(".py"):
            pat = re.compile(r'[ \t]*"@\\"[^"\\]*?/toolchain/' + name + r'\\"",\n')
            rep = "".join('        "%s",\n' % f for f in flags)
        else:
            pat = re.compile(r'@\\?"[^"]*?/toolchain/' + name + r'\\?"')
            rep = " ".join(flags)
        s2, n = pat.subn(rep, s)
        if n: open(t, "w").write(s2); print(f"   {os.path.basename(t)}: inlined {name} ({n})")
PY

leftover=$(grep -rl "$LB" "$LIBS/$TARGET/flags" "$LIBS/$TARGET/pioarduino-build.py" 2>/dev/null || true)
[ -z "$leftover" ] || { echo "ERROR: build-tree paths remain in: $leftover"; exit 1; }

echo "==> done"
grep -m1 '"version"' "$FW/package.json"
grep -m1 '"version"' "$LIBS/package.json"
echo -n "AUTOIP: "; grep -c '^CONFIG_LWIP_AUTOIP=y' "$LIBS/$TARGET/sdkconfig"
