#!/usr/bin/env bash
# Builds BadgeMic.app (release) into companion/macos/dist/. No Xcode needed, only Command Line Tools + Homebrew opus.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

APP="$ROOT/dist/BadgeMic.app"
BUNDLE_ID="${BUNDLE_ID:-tw.kaiwu.badgemic}"
VERSION="${VERSION:-0.1.1}"

swift build -c release
BIN_DIR="$(swift build -c release --show-bin-path)"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BIN_DIR/BadgeMic" "$APP/Contents/MacOS/BadgeMic"
cp "$ROOT/Resources/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"

# Bundle the virtual microphone driver (signed by its own build script with the same identity
# order) so the app can install it with one click. Nested code lives under Contents/Library.
"$ROOT/Driver/build.sh" >/dev/null
mkdir -p "$APP/Contents/Library/Audio"
cp -R "$ROOT/dist/BadgeMicAudio.driver" "$APP/Contents/Library/Audio/"

# Bundle libopus so the app does not depend on Homebrew at run time.
# Bundle a libopus built for macOS 14+ (Homebrew's targets this Mac's macOS; see build-opus.sh).
OPUS_LIB="$("$ROOT/scripts/build-opus.sh" | tail -1)"
cp "$OPUS_LIB" "$APP/Contents/Frameworks/libopus.0.dylib"
chmod u+w "$APP/Contents/Frameworks/libopus.0.dylib"
install_name_tool -id "@rpath/libopus.0.dylib" "$APP/Contents/Frameworks/libopus.0.dylib"
OLD="$(otool -L "$APP/Contents/MacOS/BadgeMic" | awk '/libopus/ {print $1}' | head -1)"
if [ -n "$OLD" ]; then
  install_name_tool -change "$OLD" "@rpath/libopus.0.dylib" "$APP/Contents/MacOS/BadgeMic"
fi
install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP/Contents/MacOS/BadgeMic" 2>/dev/null || true

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>BadgeMic</string>
  <key>CFBundleDisplayName</key><string>BadgeMic</string>
  <key>CFBundleIdentifier</key><string>${BUNDLE_ID}</string>
  <key>CFBundleExecutable</key><string>BadgeMic</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSUIElement</key><true/>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>BadgeMic 使用藍牙連接 Passport 名牌,接收語音與按鍵並上傳名牌畫面。</string>
  <key>NSBluetoothPeripheralUsageDescription</key>
  <string>BadgeMic 使用藍牙連接 Passport 名牌。</string>
  <key>NSMicrophoneUsageDescription</key>
  <string>BadgeMic 不錄音;此權限僅用於列出與切換音訊裝置。</string>
  <key>NSAppleEventsUsageDescription</key>
  <string>BadgeMic 需要此權限來執行您設定的按鍵動作。</string>
</dict>
</plist>
PLIST

# Code signing. A stable identity keeps the designated requirement constant across rebuilds, so macOS
# privacy grants (Accessibility/TCC) survive. Order: $BADGEMIC_SIGN_IDENTITY, first Developer ID Application,
# first Apple Development, else ad-hoc. Identities are selected by SHA-1 hash, never by name.
find_identity() {  # $1 = certificate kind prefix
  security find-identity -v -p codesigning | awk -v k="$1" 'index($0, "\"" k ":") {print $2; exit}'
}
IDENTITY="${BADGEMIC_SIGN_IDENTITY:-}"
KIND="custom"
if [ -n "$IDENTITY" ] && security find-identity -v -p codesigning | grep -F "$IDENTITY" | grep -q '"Developer ID Application:'; then
  KIND="developer-id"   # an explicit Developer ID still needs a secure timestamp for notarization
fi
if [ -z "$IDENTITY" ]; then
  IDENTITY="$(find_identity "Developer ID Application")"; KIND="developer-id"
fi
if [ -z "$IDENTITY" ]; then
  IDENTITY="$(find_identity "Apple Development")"; KIND="apple-development"
fi

SIGN_OPTS=()
if [ -n "$IDENTITY" ]; then
  echo "Signing with identity ${IDENTITY} (${KIND})"
  case "$KIND" in
    developer-id) SIGN_OPTS=(--options runtime --timestamp) ;;
    *) SIGN_OPTS=(--options runtime --timestamp=none) ;;
  esac
else
  echo "WARNING: no signing identity found; using ad-hoc signing. Accessibility permission will need re-granting after every rebuild." >&2
  IDENTITY="-"
fi

ENTITLEMENTS="$ROOT/scripts/BadgeMic.entitlements"
ENT_OPTS=()
if [ "$IDENTITY" != "-" ]; then ENT_OPTS=(--entitlements "$ENTITLEMENTS"); fi

# Sign the dylib first, then the bundle.
codesign --force --sign "$IDENTITY" ${SIGN_OPTS[@]+"${SIGN_OPTS[@]}"} "$APP/Contents/Frameworks/libopus.0.dylib"
codesign --force --sign "$IDENTITY" ${SIGN_OPTS[@]+"${SIGN_OPTS[@]}"} ${ENT_OPTS[@]+"${ENT_OPTS[@]}"} \
  --identifier "$BUNDLE_ID" "$APP"
codesign --verify --deep --strict --verbose=1 "$APP"

echo "Built: $APP"
