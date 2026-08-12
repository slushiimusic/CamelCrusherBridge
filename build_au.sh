#!/bin/bash
set -e
cd "$(dirname "$0")"

OUT="${1:-$HOME/Library/Audio/Plug-Ins/Components/CamelCrusher.component}"
C="$OUT/Contents"

echo "==> compiling x86_64 helper"
clang -arch x86_64 -O2 -o helper_x86 helper.c

echo "==> compiling arm64 Audio Unit"
clang -arch arm64 -O2 -fPIC -bundle -fobjc-arc \
      -o au_arm64 au.c bridge.c gui.m auview.m \
      -framework Cocoa -framework AudioUnit -framework AudioToolbox \
      -framework CoreAudio -framework CoreFoundation \
      -Wl,-exported_symbol,_CamelCrusherAUFactory

echo "==> assembling bundle at: $OUT"
if [ -d "$OUT" ]; then
    rm -rf "$OUT"/* 2>/dev/null || true
    rm -rf "$OUT"/.[!.]* 2>/dev/null || true
else
    mkdir -p "$OUT" || { echo "ERROR: cannot create $OUT" >&2; exit 1; }
fi
mkdir -p "$C/MacOS" "$C/Resources"

cp au_arm64   "$C/MacOS/CamelCrusher"
cp helper_x86 "$C/MacOS/CamelCrusherHelper"

ORIG="${CC_ORIG:-/Library/Application Support/Camel Audio/CamelCrusherOriginal.vst}"
SKIN="${CC_SKIN:-/Library/Application Support/Camel Audio/CamelCrusherData/Skins}"
[ -d "$ORIG" ] || { echo "ERROR: pristine original missing at $ORIG" >&2; exit 1; }
cp -R "$ORIG" "$C/Resources/CamelCrusher.vst"
[ -d "$SKIN" ] && { mkdir -p "$C/Resources/Skins"; cp -R "$SKIN/default" "$C/Resources/Skins/"; }

# AudioComponents entry replaces the original's removed Component Manager 'thng'
# resource. type/subtype/manufacturer match the original exactly (aumf/CaCr/CamA)
# so host projects referencing Camel Audio's AU re-link to this build.
cat > "$C/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>English</string>
  <key>CFBundleExecutable</key><string>CamelCrusher</string>
  <key>CFBundleIdentifier</key><string>com.camelaudio.au.CamelCrusher</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>CamelCrusher</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>1.01</string>
  <key>CFBundleVersion</key><string>1.01</string>
  <key>CFBundleSignature</key><string>????</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>AudioComponents</key>
  <array>
    <dict>
      <key>type</key><string>aumf</string>
      <key>subtype</key><string>CaCr</string>
      <key>manufacturer</key><string>CamA</string>
      <key>name</key><string>Camel Audio: CamelCrusher</string>
      <key>description</key><string>CamelCrusher</string>
      <key>version</key><integer>257</integer>
      <key>factoryFunction</key><string>CamelCrusherAUFactory</string>
      <key>sandboxSafe</key><false/>
    </dict>
  </array>
</dict>
</plist>
PLIST
printf 'BNDL????' > "$C/PkgInfo"

echo "==> signing"
xattr -cr "$OUT"
codesign --force --sign - "$C/MacOS/CamelCrusherHelper"
codesign --force --sign - "$OUT"

echo "==> result"
echo "  AU:     $(lipo -archs "$C/MacOS/CamelCrusher")"
echo "  helper: $(lipo -archs "$C/MacOS/CamelCrusherHelper")"
