#!/bin/bash
# Builds a macOS installer disk image for a JUCE plugin project:
#   dist/<Product>-<version>-macOS.dmg
#     Install <Product>.pkg     (system-wide VST3 + AU + standalone app)
#     Uninstall <Product>.app   (double-click uninstaller)
#     READ ME FIRST.txt
#
# Names come from CMakeLists.txt:
#   project(<Target> VERSION x.y.z ...)      CMake target + artefact folder
#   PRODUCT_NAME "<Product>"                 bundle / display name (defaults to the target)
# and the JUCE products are expected at <Target>_artefacts/Release/{VST3,AU,Standalone}.
#
# Usage: installer/build_installer.sh [path/to/<Product>_artefacts/Release]
#   env: DESCRIPTION  one-line description used on the installer welcome page
#        BUNDLE_PREFIX reverse-DNS prefix (default com.garyanewsome)
#
# Optional signing (needs an Apple Developer ID; unset = unsigned build):
#   APP_SIGN_IDENTITY="Developer ID Application: Name (TEAMID)"
#   INSTALLER_SIGN_IDENTITY="Developer ID Installer: Name (TEAMID)"
#   NOTARY_PROFILE="name of a notarytool keychain profile"
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

TARGET="$(sed -n 's/^project(\([A-Za-z0-9_]*\) VERSION [0-9.]*.*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
VERSION="$(sed -n 's/^project([A-Za-z0-9_]* VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$TARGET" ] && [ -n "$VERSION" ] || { echo "could not read project() from CMakeLists.txt" >&2; exit 1; }
PRODUCT="$(sed -n 's/^[[:space:]]*PRODUCT_NAME "\(.*\)".*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
PRODUCT="${PRODUCT:-$TARGET}"

BUNDLE_PREFIX="${BUNDLE_PREFIX:-com.garyanewsome}"
DESCRIPTION="${DESCRIPTION:-a software synthesizer plugin}"
ART="${1:-$ROOT/cmake-build/${TARGET}_artefacts/Release}"
OUT="$ROOT/dist"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PKG_ID="$BUNDLE_PREFIX.$TARGET.pkg"

for b in "VST3/$PRODUCT.vst3" "AU/$PRODUCT.component" "Standalone/$PRODUCT.app"; do
  [ -d "$ART/$b" ] || { echo "missing build artefact: $ART/$b" >&2; exit 1; }
done
echo "Building $PRODUCT $VERSION installer from $ART"

# Render the templates (@PRODUCT@ etc.) into a scratch copy.
TPL="$WORK/tpl"
mkdir -p "$TPL"
cp -R "$HERE/scripts" "$HERE/resources" "$HERE/uninstall.sh" "$HERE/Uninstall.applescript" "$TPL/"
find "$TPL" -type f \( -name '*.txt' -o -name '*.sh' -o -name '*.applescript' -o -name 'postinstall' \) -print0 |
  xargs -0 sed -i '' -e "s|@PRODUCT@|$PRODUCT|g" -e "s|@TARGET@|$TARGET|g" -e "s|@BUNDLE_PREFIX@|$BUNDLE_PREFIX|g" -e "s|@DESCRIPTION@|$DESCRIPTION|g"
chmod +x "$TPL/uninstall.sh" "$TPL/scripts/postinstall"

# --- payload -----------------------------------------------------------
PAYLOAD="$WORK/payload"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/VST3" \
         "$PAYLOAD/Library/Audio/Plug-Ins/Components" \
         "$PAYLOAD/Applications"
ditto "$ART/VST3/$PRODUCT.vst3"      "$PAYLOAD/Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3"
ditto "$ART/AU/$PRODUCT.component"   "$PAYLOAD/Library/Audio/Plug-Ins/Components/$PRODUCT.component"
ditto "$ART/Standalone/$PRODUCT.app" "$PAYLOAD/Applications/$PRODUCT.app"

# Strip extended attributes so no "._" AppleDouble files end up in the package.
xattr -cr "$PAYLOAD" 2>/dev/null || true
export COPYFILE_DISABLE=1

BUNDLES=("$PAYLOAD/Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3"
         "$PAYLOAD/Library/Audio/Plug-Ins/Components/$PRODUCT.component"
         "$PAYLOAD/Applications/$PRODUCT.app")
if [ -n "${APP_SIGN_IDENTITY:-}" ]; then
  for b in "${BUNDLES[@]}"; do
    codesign --force --deep --options runtime --timestamp --sign "$APP_SIGN_IDENTITY" "$b"
  done
else
  # No Developer ID: still ad-hoc sign (Apple Silicon refuses unsigned code).
  for b in "${BUNDLES[@]}"; do codesign --force --deep --sign - "$b"; done
fi

# --- component package -------------------------------------------------
# Never let the installer "relocate" the bundles to some other copy on disk.
pkgbuild --analyze --root "$PAYLOAD" "$WORK/components.plist" >/dev/null
/usr/bin/python3 - "$WORK/components.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]
d = plistlib.load(open(p, "rb"))
for e in d:
    e["BundleIsRelocatable"] = False
    e["BundleOverwriteAction"] = "upgrade"
plistlib.dump(d, open(p, "wb"))
PY

pkgbuild --root "$PAYLOAD" \
         --component-plist "$WORK/components.plist" \
         --identifier "$PKG_ID" \
         --version "$VERSION" \
         --install-location "/" \
         --ownership recommended \
         --scripts "$TPL/scripts" \
         "$WORK/component.pkg" >/dev/null

# --- product archive (the thing people double-click) -------------------
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$PRODUCT</title>
  <welcome file="welcome.txt"/>
  <conclusion file="conclusion.txt"/>
  <options customize="never" require-scripts="false" hostArchitectures="x86_64,arm64"/>
  <domains enable_localSystem="true"/>
  <volume-check>
    <allowed-os-versions><os-version min="13.0"/></allowed-os-versions>
  </volume-check>
  <choices-outline><line choice="default"><line choice="$PKG_ID"/></line></choices-outline>
  <choice id="default"/>
  <choice id="$PKG_ID" visible="false"><pkg-ref id="$PKG_ID"/></choice>
  <pkg-ref id="$PKG_ID" version="$VERSION" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
XML

STAGE="$WORK/dmg"
mkdir -p "$STAGE"
productbuild --distribution "$WORK/distribution.xml" \
             --resources "$TPL/resources" \
             --package-path "$WORK" \
             "$WORK/unsigned.pkg" >/dev/null

if [ -n "${INSTALLER_SIGN_IDENTITY:-}" ]; then
  productsign --sign "$INSTALLER_SIGN_IDENTITY" "$WORK/unsigned.pkg" "$STAGE/Install $PRODUCT.pkg"
else
  cp "$WORK/unsigned.pkg" "$STAGE/Install $PRODUCT.pkg"
fi

# --- uninstaller app ---------------------------------------------------
osacompile -o "$STAGE/Uninstall $PRODUCT.app" "$TPL/Uninstall.applescript"
cp "$TPL/uninstall.sh" "$STAGE/Uninstall $PRODUCT.app/Contents/Resources/uninstall.sh"
chmod +x "$STAGE/Uninstall $PRODUCT.app/Contents/Resources/uninstall.sh"
if [ -n "${APP_SIGN_IDENTITY:-}" ]; then
  codesign --force --deep --options runtime --timestamp --sign "$APP_SIGN_IDENTITY" "$STAGE/Uninstall $PRODUCT.app"
fi

cp "$TPL/resources/README-FIRST.txt" "$STAGE/READ ME FIRST.txt"

# --- disk image --------------------------------------------------------
mkdir -p "$OUT"
DMG="$OUT/$PRODUCT-$VERSION-macOS.dmg"
rm -f "$DMG"
hdiutil create -quiet -volname "$PRODUCT $VERSION" -srcfolder "$STAGE" -format UDZO -ov "$DMG"

if [ -n "${NOTARY_PROFILE:-}" ]; then
  xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG"
fi

echo "Built: $DMG"
