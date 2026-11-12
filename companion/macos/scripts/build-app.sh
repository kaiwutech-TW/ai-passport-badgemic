#!/usr/bin/env bash
# Builds BadgeMic.app (release) into companion/macos/dist/. No Xcode needed, only Command Line Tools + Homebrew opus.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

APP="$ROOT/dist/BadgeMic.app"
BUNDLE_ID="${BUNDLE_ID:-tw.kaiwu.badgemic}"
VERSION="${VERSION:-0.1.0}"

swift build -c release
BIN_DIR="$(swift build -c release --show-bin-path)"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BIN_DIR/BadgeMic" "$APP/Contents/MacOS/BadgeMic"

# Bundle libopus so the app does not depend on Homebrew at run time.
OPUS_LIB="$(readlink -f "$(brew --prefix opus)/lib/libopus.0.dylib")"
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

# Ad-hoc signature (local use). Sign the dylib first, then the bundle.
codesign --force --sign - "$APP/Contents/Frameworks/libopus.0.dylib"
codesign --force --sign - --identifier "$BUNDLE_ID" "$APP"
codesign --verify --verbose=1 "$APP"

echo "Built: $APP"
