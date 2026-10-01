#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_BUILD="$ROOT/build/ios-frame-upload-tests"
mkdir -p "$TEST_BUILD"
# Reuse dependency includes from the configured device build, compiling for the
# host GPU. Requires a macOS Vulkan loader or MoltenVK (brew install molten-vk).
python3 - "$ROOT" "$TEST_BUILD" <<'PY'
import json
import shlex
import subprocess
import sys
from pathlib import Path
root, output = map(Path, sys.argv[1:])
commands = json.loads((root / 'build/ios-device/compile_commands.json').read_text())
entry = next(c for c in commands if c['file'].endswith('vkutil/src/objects.cpp'))
args = shlex.split(entry['command'])
includes = []
for i, arg in enumerate(args):
    if arg.startswith('-I'):
        includes.append(arg)
    elif arg == '-isystem':
        includes.extend([arg, args[i + 1]])
subprocess.run(['c++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
                '-DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1', '-DFMT_HEADER_ONLY', *includes,
                str(root / 'vita3k/vkutil/tests/frame_upload.cpp'),
                str(root / 'vita3k/vkutil/src/objects.cpp'), '-o', str(output / 'frame-upload-tests')], check=True)
PY
TEST_LOADER="${VULKAN_LOADER:-$(brew --prefix molten-vk)/lib/libMoltenVK.dylib}"
"$TEST_BUILD/frame-upload-tests" "$TEST_LOADER"
