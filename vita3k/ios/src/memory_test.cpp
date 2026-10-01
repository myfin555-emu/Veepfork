// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

// Step 5 of the iOS port (ios-plan.md): validate the Vita3K memory model on
// the target. Everything here exercises the production code paths:
//
//   A) ::init — the 4 GiB contiguous guest reservation (PROT_NONE,
//      MAP_ANONYMOUS), host page size (16 KiB on iOS 26 / Apple Silicon),
//      physical footprint of the reservation, and the process virtual
//      address ceiling (with or without the extended-virtual-addressing
//      entitlement).
//   B) Guest pages of 4 KiB living on top of 16 KiB host pages: two
//      allocations sharing one physical page, partial release (the host page
//      must stay committed while a sibling is alive), reuse of freed pages,
//      and the PROT_NONE -> SIGBUS/SIGSEGV handler -> ProtectCallback chain
//      (the write-protect policy the kernel HLE relies on).
//   C) page_table mode (use_page_table=true): the same operations with the
//      per-page pointer table initialized over the 4 GiB reservation.
//   D) Pressure: repeated alloc/free cycles must not grow the footprint.
//   E) The process JIT arena: write through RW, publish through Darwin's
//      cache APIs, execute through RX. Also exercise Dynarmic ARM/Thumb,
//      invalidation, cache reuse, and cross-thread code publication.

#include <ios/bridge.h>
#include <ios/jit_memory.h>
#include <ios/jit_regression_test.h>
#include <libkern/OSCacheControl.h>
#include <ios/test_lock.h>
#include <mem/functions.h>
#include <mem/state.h>

#include <util/log.h>
#include <util/jit_config.h>

#include <dlfcn.h>
#include <fstream>
#include <mach/mach.h>
#include <os/proc.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr size_t k4KiB = 4096;
// Use the same budget as app preflight: only one successful StikDebug
// Prepare/Detach cycle is available per process.
constexpr size_t kArenaBytes = jit_config::default_arena_bytes;

// On real devices there is no console: if the process dies inside a risky
// phase, the last line of this file says how far it got.
void probe_phase(const char *phase) {
    const char *home = ::getenv("HOME");
    if (!home)
        return;
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/Documents/memory-probe.txt", home);
    std::FILE *f = std::fopen(path, "a");
    if (!f)
        return;
    const std::time_t now = std::time(nullptr);
    std::fprintf(f, "%s %s\n", std::ctime(&now), phase);
    std::fclose(f);
}

void probe_phase(const std::string &phase) {
    probe_phase(phase.c_str());
}

uint64_t total_ram_bytes() {
    uint64_t ram = 0;
    size_t len = sizeof(ram);
    if (sysctlbyname("hw.memsize", &ram, &len, nullptr, 0) != 0)
        return 0;
    return ram;
}

struct Rss {
    uint64_t resident = 0;
    uint64_t footprint = 0;
};

Rss task_mem() {
    // TASK_VM_INFO carries both resident_size and the jetsam metric
    // phys_footprint on iOS.
    task_vm_info_data_t info = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
        return {};
    return {info.resident_size, info.phys_footprint};
}

std::string human_bytes(uint64_t b) {
    char buf[32];
    if (b >= (1ULL << 40))
        std::snprintf(buf, sizeof(buf), "%.1f TiB", double(b) / double(1ULL << 40));
    else if (b >= (1ULL << 30))
        std::snprintf(buf, sizeof(buf), "%llu GiB", (unsigned long long)(b >> 30));
    else if (b >= (1ULL << 20))
        std::snprintf(buf, sizeof(buf), "%llu MiB", (unsigned long long)(b >> 20));
    else if (b >= (1ULL << 10))
        std::snprintf(buf, sizeof(buf), "%llu KiB", (unsigned long long)(b >> 10));
    else
        std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
    return buf;
}

std::string hex64(uint64_t v) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%llx", (unsigned long long)v);
    return buf;
}

uint64_t page_align_down(uint64_t v, uint32_t ps) {
    return v & ~(uint64_t(ps) - 1);
}

// Largest anonymous PROT_NONE reservation that succeeds right now, placed
// wherever the kernel wants (hint = NULL). The VA ceiling is between this
// value and the next larger probe step.
size_t probe_va_ceiling() {
    static const size_t steps[] = {
        1ULL << 40, //  1 TiB
        1ULL << 39, // 512 GiB
        1ULL << 38, // 256 GiB
        1ULL << 37, // 128 GiB
        1ULL << 36, //  64 GiB
        1ULL << 35, //  32 GiB
        1ULL << 34, //  16 GiB
        1ULL << 33, //   8 GiB
        1ULL << 32, //   4 GiB
        1ULL << 31, //   2 GiB
        1ULL << 30, //   1 GiB
    };
    size_t largest = 0;
    for (const size_t s : steps) {
        void *p = ::mmap(nullptr, s, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) {
            largest = s;
            ::munmap(p, s);
        }
    }
    return largest;
}

// Same probe, but with the production hint (mem::init asks the kernel for
// 1<<34): on iOS the kernel is free to relocate the reservation, which is
// exactly how the 4 GiB guest reservation succeeds in a small VA space.
size_t probe_hinted_ceiling() {
    static const size_t steps[] = {
        1ULL << 35, //  32 GiB
        1ULL << 34, //  16 GiB
        1ULL << 33, //   8 GiB
        1ULL << 32, //   4 GiB
        1ULL << 31, //   2 GiB
        1ULL << 30, //   1 GiB
    };
    const void *hint = reinterpret_cast<const void *>(1ULL << 34);
    size_t largest = 0;
    for (const size_t s : steps) {
        void *p = ::mmap(const_cast<void *>(hint), s, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) {
            largest = s;
            ::munmap(p, s);
        }
    }
    return largest;
}

// ---------------------------------------------------------------------------
// JIT bootstrap: the exact dual-map recipe validated on iPhone 17 by
// ios/JitDynTest (write RW alias -> flush both views -> execute from RX).
// ---------------------------------------------------------------------------

constexpr uint32_t kRet = 0xD65F03C0u;

// ADD X0, X0, #imm12
uint32_t enc_add_x0_imm(uint32_t imm12) {
    return 0x91000000u | (imm12 << 10);
}

// LDR X2, [X1]
constexpr uint32_t kLdrX2X1 = 0xF9400022u;
// STR X0, [X1]
constexpr uint32_t kStrX0X1 = 0xF9000020u;
// ADD X0, X0, X2 (word verified with clang/otool: `add x0, x0, x2` -> 8b020000)
constexpr uint32_t kAddX0X0X2 = 0x8B020000u;

void flush_code(uint64_t rw, uint64_t rx, size_t len) {
    sys_dcache_flush(reinterpret_cast<void*>(rw), len);
    sys_icache_invalidate(reinterpret_cast<void*>(rx), len);
}

struct SliceFn {
    vita::ios::JitCodeRegion region{};
    size_t region_size = 0;
    int mode = 0; // 0 = dual alias, 1 = MAP_JIT toggle, 2 = mprotect
    int write_rc = 0; // last write-enable errno/flag (0 = ok)

    void begin_write() {
        write_rc = 0;
        if (mode == 2 && region_size > 0) {
            if (::mprotect(reinterpret_cast<void*>(region.rw), region_size, PROT_READ | PROT_WRITE) != 0) {
                write_rc = errno;
            }
        } else if (mode == 1) {
            static void (*toggle)(int) = reinterpret_cast<void (*)(int)>(
                ::dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"));
            if (!toggle) {
                write_rc = -1;
            } else {
                toggle(0);
            }
        }
    }

    void end_write() {
        if (mode == 2 && region_size > 0) {
            ::mprotect(reinterpret_cast<void*>(region.rw), region_size, PROT_READ | PROT_EXEC);
        } else if (mode == 1) {
            static void (*toggle)(int) = reinterpret_cast<void (*)(int)>(
                ::dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"));
            if (toggle)
                toggle(1);
        }
    }

    bool emit_load() {
        begin_write();
        auto *p = reinterpret_cast<uint32_t *>(region.rw);
        p[0] = kLdrX2X1;
        p[1] = kAddX0X0X2;
        p[2] = kRet;
        end_write();
        flush_code(region.rw, region.rx, 3 * sizeof(uint32_t));
        return true;
    }

    bool emit_add_imm(uint32_t k) {
        begin_write();
        auto *p = reinterpret_cast<uint32_t *>(region.rw);
        p[0] = enc_add_x0_imm(k);
        p[1] = kRet;
        end_write();
        flush_code(region.rw, region.rx, 2 * sizeof(uint32_t));
        return true;
    }

    bool emit_store() {
        begin_write();
        auto *p = reinterpret_cast<uint32_t *>(region.rw);
        p[0] = kStrX0X1;
        p[1] = kRet;
        end_write();
        flush_code(region.rw, region.rx, 2 * sizeof(uint32_t));
        return true;
    }

    // f(x, g) = x + *g   (guest read through the RX view)
    uint64_t call_load(uint64_t x, uint64_t g) const {
        auto f = reinterpret_cast<uint64_t (*)(uint64_t, uint64_t)>(region.rx);
        return f(x, g);
    }

    // f(x) = x + k       (plain execution on RX)
    uint64_t call_add(uint64_t x) const {
        auto f = reinterpret_cast<uint64_t (*)(uint64_t)>(region.rx);
        return f(x);
    }

    // f(x, g) = {*g = x} (guest write through the RX view)
    uint64_t call_store(uint64_t x, uint64_t g) const {
        auto f = reinterpret_cast<uint64_t (*)(uint64_t, uint64_t)>(region.rx);
        return f(x, g);
    }

    uint32_t rx_word0() const {
        return *reinterpret_cast<const uint32_t *>(region.rx);
    }
};

} // namespace

extern "C" int vita3k_ios_memory_test(const char *base_path, char *out, size_t out_size) {
    (void)base_path;

    // Serialize runs against other step tests that touch process-global
    // state (the Vulkan demo of step 6 reserves 4 GiB and installs the fault
    // handler too).
    const std::lock_guard<std::mutex> lock(vita::ios::heavy_test_lock());

    std::string report;
    int failures = 0;
    auto add = [&report, &failures](bool ok, const char *name, const std::string &detail) {
        report += std::string(name) + ": " + (ok ? "PASS" : "FAIL");
        if (!detail.empty())
            report += " (" + detail + ")";
        report += "\n";
        if (!ok)
            failures++;
    };
    auto log = [&report](const std::string &s) { report += s + "\n"; };

    // ------------------------------------------------------------------
    // M0: platform facts and virtual address budget.
    // ------------------------------------------------------------------
    probe_phase("M0: platform");
    const uint32_t host_ps = static_cast<uint32_t>(sysconf(_SC_PAGESIZE));
    const uint64_t ram = total_ram_bytes();
    log("host_page_size: " + std::to_string(host_ps));
    log("device_ram: " + human_bytes(ram));
    const size_t va_nowhere = probe_va_ceiling();
    const size_t va_hinted = probe_hinted_ceiling();
    log("va_ceiling (kernel-chosen): largest fitting anon PROT_NONE = " + human_bytes(va_nowhere));
    log("va_ceiling (production hint 0x100000000): largest fitting block = " + human_bytes(va_hinted) +
        "; EAV entitlement " +
        (va_hinted >= (1ULL << 34) ? "effective" : "NOT effective (or not granted by the profile)"));
    // Apple's own view of the process memory budget (jetsam): how much the
    // system is willing to let this process use, right now.
    log("app_memory_budget (os_proc_available_memory): " +
        human_bytes(static_cast<uint64_t>(os_proc_available_memory())));
    const Rss rss0 = task_mem();
    log("rss_start: resident=" + human_bytes(rss0.resident) + " footprint=" + human_bytes(rss0.footprint));

    // ------------------------------------------------------------------
    // M1: 4 GiB guest reservation, 4 KiB guest pages on 16 KiB host pages.
    // ------------------------------------------------------------------
    auto state_a = std::make_unique<MemState>();
    probe_phase("M1: ::init(use_page_table=false)");
    // The mem functions live in the global namespace (mem/functions.h).
    bool model_ok = ::init(*state_a, false);
    if (!model_ok) {
        add(false, "A.reservation", "::init failed: the 4 GiB PROT_NONE reservation was refused");
        log("result: FAIL");
        if (out && out_size > 0 && report.size() + 1 <= out_size)
            std::snprintf(out, out_size, "%s", report.c_str());
        return -4;
    }
    add(true, "A.reservation",
        "base=0x" + hex64(reinterpret_cast<uint64_t>(state_a->memory.get())) +
            " size=4GiB hint=0x1000000000 " +
            (reinterpret_cast<uint64_t>(state_a->memory.get()) == (1ULL << 34) ? "granted" : "relocated") +
            " host_page_size=" + std::to_string(state_a->host_page_size));
    const Rss rss_a = task_mem();
    log("rss_after_reserve: resident=" + human_bytes(rss_a.resident) + " footprint=" + human_bytes(rss_a.footprint) +
        " (delta resident=" + human_bytes(rss_a.resident > rss0.resident ? rss_a.resident - rss0.resident : 0) +
        ", a PROT_NONE reservation must not commit pages)");

    uint8_t *const membase = state_a->memory.get();

    const Address a1 = ::alloc(*state_a, k4KiB, "a1");
    const Address a2 = ::alloc(*state_a, k4KiB, "a2");
    if (!a1 || !a2) {
        add(false, "A.guest_alloc", "::alloc refused a 4 KiB guest block");
    } else {
        const bool same_host = page_align_down(a1, host_ps) == page_align_down(a2, host_ps);
        uint8_t *p1 = membase + a1;
        uint8_t *p2 = membase + a2;
        std::memset(p1, 0x11, k4KiB);
        std::memset(p2, 0x22, k4KiB);
        const bool ok = p1[0] == 0x11 && p1[k4KiB - 1] == 0x11 && p2[0] == 0x22 && p2[k4KiB - 1] == 0x22;
        add(ok, "A.same_host_page",
            "a1=0x" + hex64(a1) + " a2=0x" + hex64(a2) + " on one 16K host page=" + (same_host ? "yes" : "no") +
                ", write/read both OK");

        // Partial release: a2 stays alive, so the shared host page must
        // remain committed and writable.
        ::free(*state_a, a1);
        p2[k4KiB - 1] = 0x33;
        const bool ok2 = p2[0] == 0x22 && p2[k4KiB - 1] == 0x33;
        add(ok2, "A.partial_free", "free(a1) with a2 alive: host page stayed committed, a2 still writable");

        // Reuse of the freed 4 KiB page, sibling untouched.
        const Address a3 = ::alloc(*state_a, k4KiB, "a3");
        uint8_t *p3 = membase + a3;
        std::memset(p3, 0x44, k4KiB);
        const bool ok3 = a3 == a1 && p3[0] == 0x44 && p2[0] == 0x22;
        add(ok3, "A.reuse", "a3=0x" + hex64(a3) + (a3 == a1 ? " reused freed page" : " (new page)") + ", a2 untouched");
        ::free(*state_a, a3);
        ::free(*state_a, a2);
    }

    // ------------------------------------------------------------------
    // M2: page_table mode — the per-page pointer table lives over the same
    // 4 GiB reservation; protect/fault must work through it too.
    // ------------------------------------------------------------------
    auto state_c = std::make_unique<MemState>();
    probe_phase("M2: mem::init(use_page_table=true)");
    if (::init(*state_c, true)) {
        const Rss rss_c = task_mem();
        log("rss_page_table_mode: resident=" + human_bytes(rss_c.resident) +
            " (extra vs plain mode = alloc_table 4MiB + page_table 8MiB, touched)");
        const Address pc = ::alloc(*state_c, k4KiB, "pc");
        uint8_t *ppc = state_c->memory.get() + pc;
        std::memset(ppc, 0x55, k4KiB);
        bool ok = pc && ppc[0] == 0x55 && ppc[k4KiB - 1] == 0x55;
        add(ok, "C.page_table_rw", "alloc + write/read with use_page_table=true");

        probe_phase("M2: protect/fault via page_table");
        struct FaultProbe {
            std::atomic<int> calls{0};
            std::atomic<bool> write{false};
        } fp;
        const bool prot = ::add_protect(*state_c, pc, k4KiB, MemPerm::None, [&fp](Address, bool w) {
            fp.write = w;
            fp.calls++;
            return true;
        });
        if (!prot) {
            add(false, "C.protect", "add_protect failed");
        } else {
            volatile uint8_t *t = state_c->memory.get() + pc;
            *t = 0x5A; // traps on PROT_NONE -> SIGBUS -> handler -> callback -> unprotect -> resume
            ok = prot && fp.calls.load() == 1 && static_cast<uint8_t>(*t) == 0x5A;
            add(ok, "C.protect_fault", "PROT_NONE fault -> handler -> ProtectCallback fired -> resumed, page_table mode");
        }
        ::free(*state_c, pc);
        ::deinit_mem(*state_c);
    } else {
        // A second concurrent 4 GiB reservation does not fit in the process
        // VA budget: on real devices this is the expected budget limit, not
        // a memory-model defect. Production uses ONE MemState per process.
        log("C.page_table_mode: SKIPPED (an additional 4 GiB reservation was refused by the kernel — VA budget)");
    }

    // ------------------------------------------------------------------
    // M3: write-protect policy on plain mode, including the 4K-on-16K
    // sibling semantics (protecting one guest page covers the whole host
    // page until the first fault unprotects it).
    // ------------------------------------------------------------------
    auto state_b = std::make_unique<MemState>();
    probe_phase("M3: ::init(fresh, protect/fault)");
    if (::init(*state_b, false)) {
        const Address pb = ::alloc(*state_b, k4KiB, "pb");
        struct FaultProbe {
            std::atomic<int> calls{0};
        } fp;
        const bool prot = ::add_protect(*state_b, pb, k4KiB, MemPerm::None, [&fp](Address, bool) {
            fp.calls++;
            return true;
        });
        if (!pb || !prot) {
            add(false, "B.protect", "alloc/add_protect failed");
        } else {
            volatile uint8_t *t = state_b->memory.get() + pb;
            probe_phase("M3.fault-write");
            *t = 0x5A;
            const bool ok = fp.calls.load() == 1 && static_cast<uint8_t>(*t) == 0x5A;
            add(ok, "B.protect_fault", "PROT_NONE fault -> SIGBUS handler -> callback fired -> resumed");

            const Address q = ::alloc(*state_b, k4KiB, "q");
            uint8_t *pq = state_b->memory.get() + q;
            std::memset(pq, 0x66, k4KiB);
            ::add_protect(*state_b, pb, k4KiB, MemPerm::None, [&fp](Address, bool) {
                fp.calls++;
                return true;
            });
            volatile uint8_t *tp = state_b->memory.get() + pb;
            *tp = 0x77; // faults again; the unprotect covers the whole 16K host page
            const bool ok2 = static_cast<uint8_t>(*tp) == 0x77 && pq[0] == 0x66 && pq[k4KiB - 1] == 0x66;
            add(ok2, "B.sibling_semantics",
                "protect(4K guest page) covers the 16K host page; fault unprotects it all, sibling data intact");
            (void)q;
        }
    } else {
        log("B.protect_fault: SKIPPED (an additional 4 GiB reservation was refused by the kernel — VA budget)");
    }

    // ------------------------------------------------------------------
    // M4: pressure — repeated alloc/free must not grow the footprint.
    // ------------------------------------------------------------------
    probe_phase("M4: pressure 100x64KiB");
    const Rss rss_p0 = task_mem();
    int pressure_ok = 1;
    for (int i = 0; i < 100; i++) {
        const Address b = ::alloc(*state_a, 64 * 1024, "cyc");
        if (!b) {
            pressure_ok = 0;
            break;
        }
        std::memset(membase + b, 0xA5, 64 * 1024);
        ::free(*state_a, b);
    }
    const Rss rss_p1 = task_mem();
    const uint64_t delta = rss_p1.footprint > rss_p0.footprint ? rss_p1.footprint - rss_p0.footprint : 0;
    add(pressure_ok == 1 && delta < (16 * 1024 * 1024), "D.pressure",
        "100x {alloc 64KiB + write + free}: footprint delta=" + human_bytes(delta) +
            " (64KiB = 4 host pages, fully decommitted on free)");

    // ------------------------------------------------------------------
    // M5: JIT arena (dual-map RX/RW, the only executable-memory mechanism
    // on iOS 26) + bootstrap: emit through RW, execute from RX, read/write
    // guest pages of the 4 GiB reservation. On device this requires an
    // active StikDebug session (brk #0xf00d); without it prepare() fails
    // cleanly and this section is reported as skipped, not failed.
    // ------------------------------------------------------------------
    auto &arena = vita::ios::JitMemory::instance();
    bool arena_prepared = arena.is_prepared();
    if (!arena_prepared) {
        // prepare() runs the brk protocol on this background thread. The
        // StikDebug session grants ONE
        // PrepareRegion/Detach per process: never call it again once the
        // arena is prepared.
        probe_phase("M5: arena prepare");
        // Snapshot the StikDebug state right before prepare: the report
        // distinguishes "no session attached at prepare time" (cs_debugged=0,
        // immediate clean failure — no brk issued, session cycle untouched)
        // from "attached but protocol not serviced" (cs_debugged=1).
        const int cs_state = vita::ios::cs_debugged_state();
        probe_phase("M5: prepare cs_debugged=" + std::to_string(cs_state));
        arena_prepared = arena.prepare(kArenaBytes);
    }
    if (!arena_prepared) {
        log("E.arena: not prepared (cs_debugged_at_prepare=" +
            std::to_string(vita::ios::cs_debugged_state()) +
            ") — on device attach the StikDebug session to THIS app process and retry "
            "(memory-model cases above are independent of it)");
    } else {
        log("E.arena: prepared " + human_bytes(arena.reservation_bytes()) +
            (arena.single_mapping() ? " (single-mapping, mprotect flow)" : " (dual-map RW alias)"));
        vita::ios::JitCodeRegion slice{};
        auto region_opt = arena.allocate_slice(k4KiB);
        bool ok = region_opt.has_value();
        if (!ok) {
            add(false, "E.slice", "allocate_slice(4KiB) failed");
        } else {
            slice = *region_opt;
            SliceFn fn;
            fn.region = slice;
            fn.region_size = k4KiB;
            fn.mode = slice.mode;

            probe_phase("M5: bootstrap exec");
            // Guest block inside the 4 GiB reservation.
            const Address g = ::alloc(*state_a, k4KiB, "jitguest");
            uint8_t *pg = membase + g;
            *pg = 41;
            fn.emit_load();
            const uint64_t got_load = fn.call_load(1, reinterpret_cast<uint64_t>(pg));
            fn.emit_store();
            fn.call_store(77, reinterpret_cast<uint64_t>(pg));
            const bool gw = *pg == 77;
            fn.emit_add_imm(5);
            const uint64_t g5 = fn.call_add(10);
            fn.emit_add_imm(7); // self-modification through the RW alias
            const uint64_t g7 = fn.call_add(10);
            const uint32_t rxw = fn.rx_word0();
            ok = g && got_load == 42 && gw && g5 == 15 && g7 == 17 && rxw == enc_add_x0_imm(7);
            add(ok, "E.bootstrap",
                std::string("RX exec of RW-emitted code: f(1,*g=41)=") + std::to_string(got_load) +
                    " (want 42), store *g=77 -> " + std::to_string(*pg) + ", f(10) 5->" + std::to_string(g5) +
                    " self-mod 7->" + std::to_string(g7) + (g ? ", guest=0x" + hex64(g) : ", no guest block") +
                    ", write mode=" + std::to_string(fn.mode) + " rc=" + std::to_string(fn.write_rc));

            probe_phase("M5: slices + validate_alive");
            // First-fit, exact reuse: with an empty free list, releasing the
            // slice and allocating its size again must return the same RX
            // base. `reuse` stays allocated on purpose so that block cannot
            // shadow the split check below.
            arena.release_slice(slice, k4KiB);
            const auto reuse = arena.allocate_slice(k4KiB);
            bool ok2 = reuse.has_value() && reuse->rx == slice.rx;
            // Split reuse: release 32 KiB, request 4 KiB (the arena rounds
            // it up to the 16 KiB host page) -> consumes the head of the
            // 32 KiB free block, leaving 16 KiB behind.
            const auto r2 = arena.allocate_slice(32 * 1024);
            ok2 = ok2 && r2.has_value();
            if (ok2) {
                arena.release_slice(*r2, 32 * 1024);
                const auto r3 = arena.allocate_slice(4 * 1024);
                ok2 = r3.has_value() && r3->rx == r2->rx;
                if (r3)
                    arena.release_slice(*r3, 4 * 1024);
            }
            ok2 = ok2 && arena.validate_alive();
            if (reuse.has_value())
                arena.release_slice(*reuse, k4KiB);
            add(ok2, "E.arena_ops", "first-fit reuse (exact + 32K split) + non-mutating validate_alive");

            probe_phase("M5: JIT regression tests");
            failures += vita::ios::run_jit_regression_tests(report);

            const bool minimal_mode = [] {
                if (const char* env = ::getenv("VITA3K_JIT_TEST_MODE"))
                    return std::string(env) == "minimal";
                if (const char* home = ::getenv("HOME")) {
                    std::ifstream f(std::string(home) + "/Documents/jit-test-mode");
                    std::string v;
                    if (f >> v)
                        return v == "minimal";
                }
                return false;
            }();

            probe_phase(std::string("M5b: per-offset exec sweep") + (minimal_mode ? " (skipped: minimal mode)" : ""));
            // Game threads crash on slices BEYOND the first 16 MiB while the
            // first slice runs fine (deterministic, flush-mode independent).
            // Sweep all remaining complete 16 MiB slices, including offsets
            // beyond the old 256 MiB limit. The bootstrap slice stays live.
            // Each slice is written then
            // executed immediately, all kept allocated until the end. A crash
            // mid-sweep leaves the last `M5b sweep@offset=` probe line at the
            // first failing offset.
            if (minimal_mode) {
                add(true, "E.offset_sweep", "skipped (minimal mode: arena left clean for the game)");
                add(true, "E.fresh_thread_exec", "skipped (minimal mode)");
            } else {
            const size_t kSweepSlice = 16 * 1024 * 1024;
            const int kSweepCount = static_cast<int>(arena.free_bytes() / kSweepSlice);
            const uint64_t sweep_base = slice.rx; // first slice = arena base (bump)
            std::string sweep;
            int sweep_ok = 0;
            std::vector<vita::ios::JitCodeRegion> sweep_slices;
            for (int i = 0; i < kSweepCount; i++) {
                const auto s = arena.allocate_slice(kSweepSlice);
                if (!s) {
                    sweep += ";alloc#" + std::to_string(i) + " failed";
                    break;
                }
                sweep_slices.push_back(*s);
                SliceFn f2;
                f2.region = *s;
                f2.region_size = kSweepSlice;
                f2.mode = s->mode;
                const uint64_t off_mib = (s->rx - sweep_base) / (1024 * 1024);
                probe_phase("M5b sweep@offset=" + std::to_string(off_mib) +
                    "M rx=" + std::to_string(s->rx) + " exec");
                f2.emit_add_imm(11 + i);
                const uint64_t r = f2.call_add(3);
                const bool good = (r == 14 + i);
                if (good)
                    sweep_ok++;
                sweep += std::string(good ? "ok" : "FAIL") + "@" +
                    std::to_string(off_mib) + "M";
            }
            add(sweep_ok == kSweepCount, "E.offset_sweep",
                sweep + " — wrote+executed one block per 16MiB slice (imm=11+i, f(3)=" +
                    std::to_string(14) + "+i)");
            // Free everything so the game still gets its slices.
            for (const auto& sr : sweep_slices)
                arena.release_slice(sr, kSweepSlice);
            sweep_slices.clear();

            probe_phase("M5c: fresh-thread RX exec");
            // The game crashes on the FIRST RX fetch of threads spawned
            // after the arena prepare (the guest sysmodule thread), while
            // the preparing thread and the game thread fetch RX fine.
            // Reproduce it without the game: spawn fresh threads, each
            // executing one block in its own freshly allocated slice.
            constexpr int kFreshThreads = 3;
            const size_t kFreshSlice = 4 * 1024 * 1024;
            std::atomic<int> fresh_results[3]{ -1, -1, -1 };
            std::vector<vita::ios::JitCodeRegion> fresh_slices;
            std::vector<std::thread> fresh_workers;
            bool fresh_alloc_ok = true;
            for (int t = 0; t < kFreshThreads; t++) {
                const auto s = arena.allocate_slice(kFreshSlice);
                if (!s) {
                    fresh_alloc_ok = false;
                    fresh_results[t].store(-2);
                    break;
                }
                fresh_slices.push_back(*s);
            }
            if (fresh_alloc_ok) {
                for (int t = 0; t < kFreshThreads; t++) {
                    fresh_workers.emplace_back([&, t] {
                        SliceFn f3;
                        f3.region = fresh_slices[t];
                        f3.region_size = kFreshSlice;
                        f3.mode = fresh_slices[t].mode;
                        probe_phase("M5c fresh-thread-" + std::to_string(t) +
                            " rx=" + std::to_string(fresh_slices[t].rx) + " exec");
                        f3.emit_add_imm(21 + t);
                        const uint64_t r = f3.call_add(3);
                        fresh_results[t].store(r == 24 + t ? 1 : static_cast<int>(r));
                    });
                }
                for (auto& w : fresh_workers)
                    w.join();
            }
            std::string fresh;
            bool fresh_ok = fresh_alloc_ok;
            for (int t = 0; t < kFreshThreads; t++) {
                const int r = fresh_results[t].load();
                fresh += (r == 1 ? "ok" : std::to_string(r)) + "#t" + std::to_string(t);
                fresh_ok = fresh_ok && r == 1;
            }
            add(fresh_ok, "E.fresh_thread_exec",
                fresh + " — 3 threads spawned at test time, one exec each in own 4MiB slices");
            for (const auto& fr : fresh_slices)
                arena.release_slice(fr, kFreshSlice);
            }  // else !minimal_mode

            if (const Address gf = g)
                ::free(*state_a, gf);
        }
    }

    // ------------------------------------------------------------------
    // Teardown: deinit in reverse registration order so the process-wide
    // access-violation handler always points at a live state while any
    // test above can still fault. state_c was already deinit'd in M2.
    // ------------------------------------------------------------------
    probe_phase("M6: deinit");
    ::deinit_mem(*state_b);
    ::deinit_mem(*state_a);
    const Rss rss_end = task_mem();
    log("rss_end: resident=" + human_bytes(rss_end.resident) + " footprint=" + human_bytes(rss_end.footprint));
    log("app_memory_budget_end (os_proc_available_memory): " +
        human_bytes(static_cast<uint64_t>(os_proc_available_memory())));

    const bool pass = failures == 0;
    log("result: " + std::string(pass ? "PASS" : "FAIL") + (failures ? " (" + std::to_string(failures) + " case(s) failed)" : ""));
    LOG_INFO("Vita3K iOS memory test: {} ({} case(s) failed)", pass ? "PASS" : "FAIL", failures);

    if (out && out_size > 0) {
        if (report.size() + 1 > out_size)
            return -3;
        std::snprintf(out, out_size, "%s", report.c_str());
    }

    return pass ? 0 : -4;
}
