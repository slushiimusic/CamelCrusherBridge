#!/bin/bash
# Builds "CamelCrusher Native.vst": an arm64 VST2 plugin plus its x86_64 helper.
set -euo pipefail
cd "$(dirname "$0")"

OUT="${1:-/Library/Audio/Plug-Ins/VST/CamelCrusher.vst}"
EXE="$(basename "${OUT%.vst}")"
C="$OUT/Contents"

echo "==> compiling x86_64 helper"
clang -arch x86_64 -O2 -o helper_x86 helper.c

echo "==> compiling arm64 plugin"
clang -arch arm64 -O2 -fPIC -bundle \
      -framework Cocoa -framework CoreFoundation \
      -fobjc-arc \
      -o plugin_arm64 plugin.c bridge.c gui.m

echo "==> assembling bundle at: $OUT"
# The parent of a system plug-in folder is root-owned, so the bundle directory
# itself may not be removable even when its contents are ours. Clear in place.
if [ -d "$OUT" ]; then
    rm -rf "$OUT"/* 2>/dev/null || true
    rm -rf "$OUT"/.[!.]* 2>/dev/null || true
else
    mkdir -p "$OUT" || { echo "ERROR: cannot create $OUT (needs sudo)" >&2; exit 1; }
fi
mkdir -p "$C/MacOS" "$C/Resources"
cp plugin_arm64 "$C/MacOS/$EXE"
cp helper_x86   "$C/MacOS/CamelCrusherHelper"

# Embed the original Intel plugin and its skin art so the bundle is self-contained.
# Source from the archived pristine copy, never from the install slot itself.
ORIG="/Library/Application Support/Camel Audio/CamelCrusherOriginal.vst"
SKIN="/Library/Application Support/Camel Audio/CamelCrusherData/Skins"
if [ ! -d "$ORIG" ]; then echo "ERROR: pristine original missing at $ORIG" >&2; exit 1; fi
cp -R "$ORIG" "$C/Resources/CamelCrusher.vst"
if [ -d "$SKIN" ]; then mkdir -p "$C/Resources/Skins"; cp -R "$SKIN/default" "$C/Resources/Skins/"; fi

cat > "$C/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>English</string>
  <key>CFBundleExecutable</key><string>__EXE__</string>
  <key>CFBundleIdentifier</key><string>com.camelaudio.vst.CamelCrusher</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>CamelCrusher</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>1.0.1</string>
  <key>CFBundleVersion</key><string>1.0.1</string>
  <key>CFBundleSignature</key><string>CcBn</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
PLIST
/usr/bin/sed -i '' "s|__EXE__|$EXE|" "$C/Info.plist"
printf 'BNDLCcBn' > "$C/PkgInfo"

echo "==> signing"
xattr -cr "$OUT"
codesign --force --sign - "$C/MacOS/CamelCrusherHelper"
codesign --force --sign - "$OUT"

echo "==> result"
echo "  plugin: $(lipo -archs "$C/MacOS/$EXE")"
echo "  helper: $(lipo -archs "$C/MacOS/CamelCrusherHelper")"
codesign -dv "$OUT" 2>&1 | grep -E 'Signature|Identifier'
