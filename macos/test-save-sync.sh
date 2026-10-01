#!/usr/bin/env bash
set -euo pipefail
VEEB_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
VEEB_TEST_ROOT="$VEEB_ROOT/build/macos-save-sync-tests"
mkdir -p "$VEEB_TEST_ROOT/Sources/Vita3K" "$VEEB_TEST_ROOT/Tests/SaveSyncTests"
cp "$VEEB_ROOT/ios/Vita3K/App/Localization.swift" "$VEEB_ROOT/ios/Vita3K/App/SaveSyncEngine.swift" "$VEEB_ROOT/ios/Vita3K/App/CloudSaveSync.swift" "$VEEB_TEST_ROOT/Sources/Vita3K/"
cp "$VEEB_ROOT/macos/CloudSaveBridge.swift" "$VEEB_TEST_ROOT/Sources/Vita3K/"
cp "$VEEB_ROOT/ios/Vita3K/InputTests/SaveSyncTests.swift" "$VEEB_TEST_ROOT/Tests/SaveSyncTests/"
cat > "$VEEB_TEST_ROOT/Package.swift" <<'SWIFT'
// swift-tools-version: 6.0
import PackageDescription
let package = Package(name: "VeebSaveSyncTests", platforms: [.macOS(.v13)], targets: [
    .target(name: "Vita3K"),
    .testTarget(name: "SaveSyncTests", dependencies: ["Vita3K"])
])
SWIFT
swift test --package-path "$VEEB_TEST_ROOT"
