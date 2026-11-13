#!/usr/bin/env bash
# Builds BadgeMic.app (with the bundled virtual microphone driver), notarizes and staples it,
# then wraps it in a signed, notarized and stapled DMG: dist/BadgeMic-<version>.dmg.
# Needs a "Developer ID Application" identity and a notarytool keychain profile
# (default name BadgeMic; create it once with `xcrun notarytool store-credentials`).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PROFILE="${NOTARY_PROFILE:-BadgeMic}"
VERSION="${VERSION:-0.1.1}"
APP="$ROOT/dist/BadgeMic.app"
DMG="$ROOT/dist/BadgeMic-$VERSION.dmg"

ID="${BADGEMIC_SIGN_IDENTITY:-$(security find-identity -v -p codesigning | awk '/"Developer ID Application:/ {print $2; exit}')}"
[ -n "$ID" ] || { echo "error: no Developer ID Application identity in the keychain" >&2; exit 1; }
export BADGEMIC_SIGN_IDENTITY="$ID"

notarize() {  # $1 = file to submit
  local out status
  out="$(xcrun notarytool submit "$1" --keychain-profile "$PROFILE" --wait --output-format json)"
  status="$(printf '%s' "$out" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin).get("status",""))')"
  echo "notarization of $(basename "$1"): $status"
  if [ "$status" != "Accepted" ]; then
    local id; id="$(printf '%s' "$out" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin).get("id",""))')"
    [ -n "$id" ] && xcrun notarytool log "$id" --keychain-profile "$PROFILE" >&2 || true
    exit 1
  fi
}

VERSION="$VERSION" ./scripts/build-app.sh
SIGINFO="$(codesign -dvv "$APP" 2>&1)"   # captured first: grep -q under pipefail would SIGPIPE codesign
case "$SIGINFO" in
  *"Authority=Developer ID Application"*) ;;
  *) echo "error: the app is not signed with Developer ID" >&2; exit 1 ;;
esac

ZIP="$ROOT/dist/BadgeMic-notarize.zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
notarize "$ZIP"
rm -f "$ZIP"
xcrun stapler staple "$APP"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "BadgeMic" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
codesign --force --sign "$ID" --timestamp "$DMG"
notarize "$DMG"
xcrun stapler staple "$DMG"

spctl -a -vv "$APP" 2>&1 | sed 's/^/app: /'
spctl -a -t open --context context:primary-signature -vv "$DMG" 2>&1 | sed 's/^/dmg: /'
echo "Packaged: $DMG"
