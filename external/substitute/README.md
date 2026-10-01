# substitute (Vita3K fork)

ARM instruction relocation library, forked from [comex/substitute](https://github.com/comex/substitute).

This fork contains only the ARM transform-dis (instruction relocation) component, adapted for use in Vita3K's taiHEN HLE implementation. It runs on the host machine and targets ARM/Thumb instruction sets.

## Changes from upstream

- **`substitute-internal.h`**: Custom adapter that forces `TARGET_arm` regardless of host architecture and provides panic/assert macros compatible with Vita3K's build environment.
- **`substitute_api.h`**: C++ wrapper providing declarations and inline implementations of `make_jump_patch()`, `jump_patch_size()`, `op16()`/`op32()`, and `arch_dis_ctx` initialization for use from C++ code.
- Only the ARM architecture files are included (no x86/ARM64).
- Generated decoder tables are pre-included (no build-time generation step).

## License

LGPLv2.1+ — see [LICENSE.txt](LICENSE.txt)
