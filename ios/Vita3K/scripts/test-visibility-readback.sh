#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_BUILD="$ROOT/build/ios-visibility-readback-tests"
mkdir -p "$TEST_BUILD"
python3 - "$ROOT" "$TEST_BUILD" <<'PY'
import json
import shlex
import subprocess
import sys
from pathlib import Path
root, output = map(Path, sys.argv[1:])
shaders = {
    'fullscreen.vert': '''#version 450
void main() {
    vec2 positions[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    gl_Position = vec4(positions[gl_VertexIndex], 0, 1);
}''',
    'solid.frag': '''#version 450
layout(location=0) out vec4 color;
void main() { color = vec4(1); }''',
}
for name, source in shaders.items():
    path = output / name
    path.write_text(source)
    subprocess.run(['glslc', str(path), '-o', str(path) + '.spv'], check=True)
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
               str(root / 'vita3k/vkutil/tests/visibility_readback.cpp'),
               str(root / 'vita3k/vkutil/src/objects.cpp'),
               '-o', str(output / 'visibility-readback-tests')], check=True)
PY
TEST_LOADER="${VULKAN_LOADER:-$(brew --prefix molten-vk)/lib/libMoltenVK.dylib}"
"$TEST_BUILD/visibility-readback-tests" "$TEST_LOADER" "$TEST_BUILD/fullscreen.vert.spv" "$TEST_BUILD/solid.frag.spv"
