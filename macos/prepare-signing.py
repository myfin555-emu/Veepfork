"""Validate Xcode's macOS iCloud profile before signing the actual Qt app."""
from pathlib import Path
import datetime
import hashlib
import plistlib
import shutil
import subprocess
import sys

def require(condition, message):
    if not condition:
        raise SystemExit(message)


signing_app, app = map(Path, sys.argv[1:3])
fingerprint, team, output = sys.argv[3:]
profile_path = signing_app / "Contents/embedded.provisionprofile"
profile = plistlib.loads(subprocess.check_output(["security", "cms", "-D", "-i", str(profile_path)]))
entitlements = plistlib.loads(subprocess.check_output(
    ["codesign", "-d", "--entitlements", ":-", str(signing_app)], stderr=subprocess.DEVNULL))
info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
container = info["Vita3KSaveContainer"]
certificates = {hashlib.sha1(cert).hexdigest().upper() for cert in profile["DeveloperCertificates"]}
require(fingerprint.upper() in certificates, "The macOS profile does not authorize this certificate.")
require(team in profile["TeamIdentifier"], "The macOS profile belongs to another team.")
require(profile["ExpirationDate"] > datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None), "Expired macOS profile.")
require("OSX" in profile["Platform"], "Expected a native macOS profile.")
for key in ("com.apple.developer.icloud-container-identifiers", "com.apple.developer.ubiquity-container-identifiers"):
    require(container in profile["Entitlements"].get(key, []), "Profile does not authorize the shared iCloud container.")
    require(container in entitlements.get(key, []), "Missing iCloud entitlement.")
require(entitlements["com.apple.application-identifier"] == team + "." + info["CFBundleIdentifier"], "App ID mismatch.")
require("CloudDocuments" in entitlements["com.apple.developer.icloud-services"], "Missing iCloud document storage.")
Path(output).write_bytes(plistlib.dumps(entitlements))
shutil.copy2(profile_path, app / "Contents/embedded.provisionprofile")
