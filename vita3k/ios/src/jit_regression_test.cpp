// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ios/jit_memory.h>
#include <ios/jit_regression_test.h>
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <dynarmic/interface/exclusive_monitor.h>
#include <oaknut/code_block.hpp>
#include <util/log.h>

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

namespace vita::ios {
namespace {

struct Slice {
    size_t size;
    JitCodeRegion region;
    explicit Slice(size_t bytes)
        : size(bytes), region(JitMemory::instance().allocate_slice(bytes).value()) {}
    ~Slice() { JitMemory::instance().release_slice(region, size); }
};

std::unique_ptr<oaknut::CodeBlock> code_block(const Slice& slice) {
    auto* rx = reinterpret_cast<uint32_t*>(slice.region.rx);
    auto* rw = reinterpret_cast<uint32_t*>(slice.region.rw);
    if (rx == rw)
        return std::make_unique<oaknut::CodeBlock>(rx, slice.size, oaknut::CodeBlock::ProtectMode::MapJit);
    return std::make_unique<oaknut::CodeBlock>(rx, rw, slice.size);
}

bool cache_lines() {
    // Execute every 64-byte half of a 128-byte interval, including a range
    // that crosses a 16-KiB page. Warm I-cache, rewrite, and execute again.
    // The old fixed 128-byte flush misses alternate I-cache lines.
    Slice slice(32768);
    auto block = code_block(slice);
    constexpr size_t begin = 60;
    constexpr size_t end = 17000;
    for (uint32_t k = 1; k <= 128; ++k) {
        block->unprotect();
        for (size_t off = begin; off < end; off += 64) {
            auto* p = reinterpret_cast<uint32_t*>(slice.region.rw + off);
            p[0] = 0x91000000 | (k << 10); // ADD X0, X0, #k
            p[1] = 0xd65f03c0; // RET
        }
        block->invalidate(reinterpret_cast<uint32_t*>(slice.region.rx + begin), end + 8 - begin);
        block->protect();
        bool ok = true;
        auto execute = [&] {
            // A thread that consumes code published by another thread
            // must synchronize its own instruction pipeline too.
            __asm__ volatile("isb" ::: "memory");
            for (size_t off = begin; off < end; off += 64) {
                auto f = reinterpret_cast<uint64_t (*)(uint64_t)>(slice.region.rx + off);
                if (f(1000) != 1000 + k)
                    ok = false;
            }
        };
        if (k % 16 == 0) {
            std::thread worker(execute);
            worker.join();
        } else {
            execute();
        }
        if (!ok)
            return false;
    }
    return true;
}

bool allocation_lifetime() {
    auto& arena = JitMemory::instance();
    const size_t before = arena.allocated_bytes();
    if (arena.allocate_slice(0) || arena.allocate_slice(std::numeric_limits<size_t>::max()))
        return false;
    constexpr size_t bytes = 32768;
    const auto a = arena.allocate_slice(bytes);
    const auto b = arena.allocate_slice(bytes);
    const auto c = arena.allocate_slice(bytes);
    if (!a || !b || !c) {
        if (a) arena.release_slice(*a, bytes);
        if (b) arena.release_slice(*b, bytes);
        if (c) arena.release_slice(*c, bytes);
        return false;
    }
    arena.release_slice(*a, bytes);
    arena.release_slice(*b, bytes); // merges two free allocations
    arena.release_slice(*a, bytes); // must not double-free the merged block
    bool ok = arena.allocated_bytes() == before + bytes;
    arena.release_slice(*c, bytes); // also collapses the preceding free tail
    ok = ok && arena.allocated_bytes() == before;
    const auto merged = arena.allocate_slice(3 * bytes);
    ok = ok && merged.has_value() && merged->rx == a->rx;
    if (merged)
        arena.release_slice(*merged, 3 * bytes);
    return ok && arena.allocated_bytes() == before;
}

bool concurrent_slice_capacity() {
    // Retail titles can keep more than sixteen CPU instances alive. Keep
    // twenty 16-MiB slices live at once and execute both ends of every slice,
    // including addresses above the old 256-MiB arena boundary.
    auto& arena = JitMemory::instance();
    const size_t before = arena.allocated_bytes();
    const size_t free_before = arena.free_bytes();
    constexpr size_t bytes = 16 * 1024 * 1024;
    constexpr unsigned count = 20;
    {
        std::vector<std::unique_ptr<Slice>> slices;
        for (unsigned i = 0; i < count; ++i)
            slices.push_back(std::make_unique<Slice>(bytes));
        if (arena.allocated_bytes() != before + count * bytes)
            return false;
        for (unsigned i = 0; i < count; ++i) {
            const auto& slice = *slices[i];
            auto block = code_block(slice);
            block->unprotect();
            for (size_t off : {size_t{0}, bytes - 8}) {
                auto* p = reinterpret_cast<uint32_t*>(slice.region.rw + off);
                p[0] = 0x91000000 | ((i + 1) << 10); // ADD X0, X0, #i+1
                p[1] = 0xd65f03c0; // RET
                block->invalidate(reinterpret_cast<uint32_t*>(slice.region.rx + off), 8);
            }
            block->protect();
        }
        // Execute after every slice is written to also catch alias overlap.
        for (unsigned i = 0; i < count; ++i) {
            for (size_t off : {size_t{0}, bytes - 8}) {
                auto f = reinterpret_cast<uint64_t (*)(uint64_t)>(slices[i]->region.rx + off);
                if (f(1000) != 1001 + i)
                    return false;
            }
        }
    }
    return arena.allocated_bytes() == before && arena.free_bytes() == free_before;
}

struct Callbacks final : Dynarmic::A32::UserCallbacks {
    std::array<uint8_t, 4096> ram{};
    Dynarmic::A32::Jit* jit = nullptr;
    bool failed = false;
    uint64_t ticks = 0;
    unsigned svc_count = 0;

    template<typename T> T read(uint32_t address) {
        T value{};
        if (address > ram.size() - sizeof(T)) { failed = true; return value; }
        std::memcpy(&value, ram.data() + address, sizeof(T));
        return value;
    }
    template<typename T> void write(uint32_t address, T value) {
        if (address > ram.size() - sizeof(T)) { failed = true; return; }
        std::memcpy(ram.data() + address, &value, sizeof(T));
    }
    uint8_t MemoryRead8(uint32_t a) override { return read<uint8_t>(a); }
    uint16_t MemoryRead16(uint32_t a) override { return read<uint16_t>(a); }
    uint32_t MemoryRead32(uint32_t a) override { return read<uint32_t>(a); }
    uint64_t MemoryRead64(uint32_t a) override { return read<uint64_t>(a); }
    void MemoryWrite8(uint32_t a, uint8_t v) override { write(a, v); }
    void MemoryWrite16(uint32_t a, uint16_t v) override { write(a, v); }
    void MemoryWrite32(uint32_t a, uint32_t v) override { write(a, v); }
    void MemoryWrite64(uint32_t a, uint64_t v) override { write(a, v); }
    void InterpreterFallback(uint32_t, size_t) override { failed = true; jit->HaltExecution(); }
    void ExceptionRaised(uint32_t, Dynarmic::A32::Exception) override { failed = true; jit->HaltExecution(); }
    void CallSVC(uint32_t) override { ++svc_count; jit->HaltExecution(); }
    void AddTicks(uint64_t n) override { ticks = n >= ticks ? 0 : ticks - n; }
    uint64_t GetTicksRemaining() override { return ticks; }
};

bool dynarmic_invalidation() {
    Slice slice(8 * 1024 * 1024);
    Callbacks cb;
    Dynarmic::ExclusiveMonitor monitor(1);
    Dynarmic::A32::UserConfig config{};
    config.callbacks = &cb;
    config.global_monitor = &monitor;
    config.arch_version = Dynarmic::A32::ArchVersion::v7;
    config.code_cache_size = slice.size;
    config.code_region = Dynarmic::A32::CodeRegion{ slice.region.rx, slice.region.rw, slice.region.mode };
    Dynarmic::A32::Jit jit(config);
    cb.jit = &jit;
    cb.write<uint32_t>(0, 0xea00000e); // B 0x40: exercise direct linking
    cb.write<uint32_t>(0x44, 0xe2901001); // ADDS r1,r0,#1
    cb.write<uint32_t>(0x48, 0xe5821000); // STR r1,[r2]
    cb.write<uint32_t>(0x4c, 0xe5923000); // LDR r3,[r2]
    cb.write<uint32_t>(0x50, 0xef000000); // SVC #0
    for (uint32_t k = 1; k <= 128; ++k) {
        cb.write<uint32_t>(0x40, 0xe3a00000 | k); // MOV r0,#k
        // After warm execution, the predecessor is already linked to the
        // changed block. Invalidation must unlink that old branch as well.
        jit.InvalidateCacheRange(0x40, 4);
        if (k % 17 == 0)
            jit.ClearCache();
        for (int warm = 0; warm < 2; ++warm) {
            jit.Regs().fill(0);
            jit.Regs()[2] = 0x200;
            jit.SetCpsr(0x10);
            cb.ticks = 1000;
            cb.svc_count = 0;
            jit.Run();
            if (cb.failed || cb.svc_count != 1 || jit.Regs()[0] != k || jit.Regs()[3] != k + 1 || cb.read<uint32_t>(0x200) != k + 1)
                return false;
        }
    }
    cb.write<uint16_t>(0x100, 0x2007); // Thumb MOVS r0,#7
    cb.write<uint16_t>(0x102, 0x3005); // ADDS r0,#5
    cb.write<uint16_t>(0x104, 0xdf00); // SVC #0
    jit.Regs()[15] = 0x100;
    jit.SetCpsr(0x30);
    cb.ticks = 1000;
    cb.svc_count = 0;
    jit.Run();
    return !cb.failed && cb.svc_count == 1 && jit.Regs()[0] == 12;
}

} // namespace

int run_jit_regression_tests(std::string& report) {
    int failures = 0;
    for (const auto& [name, run] : std::array<std::pair<const char*, bool (*)()>, 4>{ {
             { "cache_lines", cache_lines },
             { "allocation_lifetime", allocation_lifetime },
             { "concurrent_slice_capacity", concurrent_slice_capacity },
             { "dynarmic_invalidation", dynarmic_invalidation },
         } }) {
        LOG_INFO("JIT regression: starting {}", name);
        bool ok = false;
        try { ok = run(); } catch (const std::exception& e) {
            LOG_ERROR("JIT regression {}: {}", name, e.what());
        }
        report += std::string("E.regression.") + name + (ok ? ": PASS\n" : ": FAIL\n");
        LOG_INFO("JIT regression {}: {}", name, ok ? "PASS" : "FAIL");
        failures += !ok;
    }
    return failures;
}

} // namespace vita::ios
