#!/usr/bin/env bash
set -euo pipefail

VEEB_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
source "$VEEB_ROOT/.ci/apple-build-common.sh"

usage() {
    cat <<'EOF'
Usage: ./build-macos.sh [--clean]
Build and sign the Release app for this Mac's architecture.
Outputs: dist/macos/Veeb.app and dist/macos/Veeb.zip.
--clean removes build/macos-ninja before compiling.
Optional environment: VEEB_JOBS, VEEB_SIGNING_IDENTITY (name or SHA-1), VEEB_TEAM_ID, Qt6_ROOT.
Requires Xcode with a developer account, XcodeGen, CMake, Ninja, Python 3.12+, and Qt 6.11+.
Uses Apple Development/Mac Developer and an automatic iCloud-enabled provisioning profile.
The development-signed app runs on Macs authorized by that profile.
Produces signed artifacts; does not submit them to Apple's notarization service.
EOF
}

apple_build_options "$@"
apple_build_require openssl xcodegen
apple_build_identity macos-icloud
cd "$VEEB_ROOT"
apple_build_clean "$VEEB_ROOT/build/macos-ninja"

cmake --preset macos-ninja -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
    -DUSE_LTO=RELEASE_ONLY -DPython3_EXECUTABLE="$VEEB_PYTHON"
# A portable Qt bundle needs the deploy tool, not only Qt's headers/libraries.
VEEB_MACDEPLOYQT=$(sed -n 's/^MACDEPLOYQT_EXECUTABLE:FILEPATH=//p' build/macos-ninja/CMakeCache.txt)
if [[ ! -x "$VEEB_MACDEPLOYQT" ]]; then
    echo 'macdeployqt was not found. Install Qt deployment tools or set Qt6_ROOT.' >&2
    exit 1
fi
cmake --build --preset macos-ninja-release --target vita3k --parallel "$VEEB_JOBS"

mkdir -p "$VEEB_ROOT/dist/macos"
VEEB_PACKAGE=$(mktemp -d "$VEEB_ROOT/build/macos-ninja/package.XXXXXX")
trap 'rm -rf -- "$VEEB_PACKAGE"' EXIT
ditto "$VEEB_ROOT/build/macos-ninja/bin/Release/Veeb.app" "$VEEB_PACKAGE/Veeb.app"
# Resolve dependencies of the plugins that CMake deployed, using the final
# bundle name/location, before applying the certificate signature.
"$VEEB_MACDEPLOYQT" "$VEEB_PACKAGE/Veeb.app"
# iCloud entitlements must be authorized by an embedded macOS profile. Xcode
# resolves that profile using the App ID/container already shared with iOS.
xcodegen generate --spec macos/signing/project.yml
xcodebuild -project macos/signing/VeebSigning.xcodeproj -scheme VeebSigning \
    -configuration Release -destination 'platform=macOS' \
    -derivedDataPath "$VEEB_ROOT/build/macos-ninja/signing" \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration \
    DEVELOPMENT_TEAM="$VEEB_TEAM_ID" CODE_SIGN_IDENTITY="${VEEB_CERT_NAME%%:*}" build
VEEB_SIGNING_APP="$VEEB_ROOT/build/macos-ninja/signing/Build/Products/Release/VeebSigning.app"
python3 macos/prepare-signing.py "$VEEB_SIGNING_APP" "$VEEB_PACKAGE/Veeb.app" \
    "$VEEB_CERT_SHA1" "$VEEB_TEAM_ID" "$VEEB_PACKAGE/Veeb.entitlements"
apple_build_sign_bundle "$VEEB_PACKAGE/Veeb.app" "$VEEB_PACKAGE/Veeb.entitlements"
ditto -c -k --sequesterRsrc --keepParent "$VEEB_PACKAGE/Veeb.app" "$VEEB_PACKAGE/Veeb.zip"
rm -rf -- "$VEEB_ROOT/dist/macos/Veeb.app"
mv "$VEEB_PACKAGE/Veeb.app" "$VEEB_ROOT/dist/macos/Veeb.app"
mv -f "$VEEB_PACKAGE/Veeb.zip" "$VEEB_ROOT/dist/macos/Veeb.zip"
printf '\nBuilt and signed:\n  %s\n  %s\n' "$VEEB_ROOT/dist/macos/Veeb.app" "$VEEB_ROOT/dist/macos/Veeb.zip"
