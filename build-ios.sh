#!/usr/bin/env bash
set -euo pipefail

VEEB_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
source "$VEEB_ROOT/.ci/apple-build-common.sh"

usage() {
    cat <<'EOF'
Usage: ./build-ios.sh [--clean]
Build and sign the Release iOS device app; write dist/ios/Veeb.app and Veeb.ipa.
--clean removes build/ios-device and build/ios-app before compiling.
Optional environment: VEEB_JOBS, VEEB_SIGNING_IDENTITY (name or SHA-1), VEEB_TEAM_ID.
Requires Xcode with a developer account, CMake, Ninja, Python 3.12+, XcodeGen and ImageMagick.
The development-signed IPA is for devices authorized by its provisioning profile.
EOF
}

apple_build_options "$@"
apple_build_require xcodegen magick openssl
apple_build_identity ios
VEEB_XCODE_IDENTITY=${VEEB_CERT_NAME%%:*}
xcrun --sdk iphoneos --show-sdk-path >/dev/null
cd "$VEEB_ROOT"
apple_build_clean "$VEEB_ROOT/build/ios-device" "$VEEB_ROOT/build/ios-app"

python3 ios/Vita3K/scripts/render-app-icon.py
cmake --preset ios-device
cmake --build --preset ios-device --target vita3k --parallel "$VEEB_JOBS"
ios/Vita3K/scripts/stage-core.sh device
xcodebuild -project ios/Vita3K/Vita3K.xcodeproj -scheme Vita3K \
    -configuration Release -destination 'generic/platform=iOS' \
    -derivedDataPath "$VEEB_ROOT/build/ios-app" -jobs "$VEEB_JOBS" \
    -allowProvisioningUpdates DEVELOPMENT_TEAM="$VEEB_TEAM_ID" \
    CODE_SIGN_IDENTITY="$VEEB_XCODE_IDENTITY" CODE_SIGN_STYLE=Automatic build

VEEB_APP="$VEEB_ROOT/build/ios-app/Build/Products/Release-iphoneos/Vita3K.app"
codesign --verify --deep --strict "$VEEB_APP"
[[ -f "$VEEB_APP/embedded.mobileprovision" ]] || { echo 'Missing iOS provisioning profile.' >&2; exit 1; }
apple_build_check_ios_profile "$VEEB_APP"
mkdir -p "$VEEB_ROOT/dist/ios"
VEEB_PACKAGE=$(mktemp -d "$VEEB_ROOT/build/ios-app/package.XXXXXX")
trap 'rm -rf -- "$VEEB_PACKAGE"' EXIT
mkdir "$VEEB_PACKAGE/Payload"
ditto "$VEEB_APP" "$VEEB_PACKAGE/Payload/Veeb.app"
# Automatic provisioning requires Xcode's generic identity selector. Pin the
# final app and nested code to our exact certificate after validating its profile.
apple_build_sign_bundle "$VEEB_PACKAGE/Payload/Veeb.app"
ditto -c -k --keepParent "$VEEB_PACKAGE/Payload" "$VEEB_PACKAGE/Veeb.ipa"
# Replace only these generated artifacts; keep other dist content intact.
rm -rf -- "$VEEB_ROOT/dist/ios/Veeb.app"
ditto "$VEEB_PACKAGE/Payload/Veeb.app" "$VEEB_ROOT/dist/ios/Veeb.app"
mv -f "$VEEB_PACKAGE/Veeb.ipa" "$VEEB_ROOT/dist/ios/Veeb.ipa"
printf '\nBuilt and signed:\n  %s\n  %s\n' "$VEEB_ROOT/dist/ios/Veeb.app" "$VEEB_ROOT/dist/ios/Veeb.ipa"
