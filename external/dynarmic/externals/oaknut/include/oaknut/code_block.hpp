// SPDX-FileCopyrightText: Copyright (c) 2022 merryhime <https://mary.rs>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>

#if defined(_WIN32)
#    define NOMINMAX
#    include <windows.h>
#elif defined(__APPLE__)
#    include <TargetConditionals.h>
#    include <dlfcn.h>
#    include <libkern/OSCacheControl.h>
#    include <pthread.h>
#    include <sys/mman.h>
#    include <unistd.h>
#else
#    include <dlfcn.h>
#    include <sys/mman.h>
#endif

namespace oaknut {

class CodeBlock {
public:
    /// Owns the code region: allocates and frees it per platform.
    explicit CodeBlock(std::size_t size)
        : m_size(size)
    {
#if defined(_WIN32)
        m_memory = (std::uint32_t*)VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#elif defined(__APPLE__)
#    if TARGET_OS_IPHONE
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
#    else
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
#    endif
#elif defined(__NetBSD__)
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_MPROTECT(PROT_READ | PROT_WRITE | PROT_EXEC), MAP_ANON | MAP_PRIVATE, -1, 0);
#elif defined(__OpenBSD__)
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
#else
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
#endif

#if defined(_WIN32)
        if (m_memory == nullptr)
#else
        if (m_memory == MAP_FAILED)
#endif
            throw std::bad_alloc{};

        m_wmemory = m_memory;
        m_owned = true;
    }

    /// Adopts an externally prepared dual-map code region (iOS JIT arena):
    /// `xmem` is the executable view (RX) and `wmem` is a writable alias (RW)
    /// of the same physical pages. The region is prepared once per process
    /// (e.g. via the iOS TXM `brk #0xf00d` protocol) and shared by several
    /// JIT instances, each adopting a private slice of it.
    ///
    /// This block does not own the memory: it never maps or unmaps it, and
    /// `protect()`/`unprotect()` are no-ops (W^X is enforced by the two
    /// views). All generated code pointers refer to the executable view;
    /// writes must go through `wptr()`.
    CodeBlock(std::uint32_t* xmem, std::uint32_t* wmem, std::size_t size)
        : m_memory(xmem)
        , m_wmemory(wmem)
        , m_size(size)
        , m_dual_map(true)
    {
        if (m_memory == nullptr || m_wmemory == nullptr || m_size == 0)
            throw std::bad_alloc{};
    }

    /// How an adopted (externally mapped) region is made writable:
    /// - `Mprotect`: flip the mapping RW/RX with mprotect (plain WX region).
    /// - `MapJit`: single RWX MAP_JIT mapping; write access toggled per
    ///   thread with `pthread_jit_write_protect_np` (resolved via dlsym on
    ///   iOS, where the symbol is not in the public headers). Emitted code
    ///   still needs an explicit instruction-cache invalidation.
    enum class ProtectMode {
        Mprotect,
        MapJit
    };

    /// Adopts an externally prepared SINGLE mapping (iOS JIT arena):
    /// executable, made writable according to `mode` (MAP_JIT toggle or
    /// mprotect). Generated code pointers and writes use the same pointer.
    /// The block never maps or unmaps the memory.
    CodeBlock(std::uint32_t* existing, std::size_t size, ProtectMode mode = ProtectMode::Mprotect)
        : m_memory(existing)
        , m_wmemory(existing)
        , m_size(size)
        , m_dual_map(false)
        , m_protect_mode(mode)
        , m_owned(false)
    {
        if (m_memory == nullptr || m_size == 0)
            throw std::bad_alloc{};
    }

    ~CodeBlock()
    {
        if (!m_owned || m_memory == nullptr)
            return;

#if defined(_WIN32)
        VirtualFree((void*)m_memory, 0, MEM_RELEASE);
#else
        munmap(m_memory, m_size);
#endif
    }

    CodeBlock(const CodeBlock&) = delete;
    CodeBlock& operator=(const CodeBlock&) = delete;
    CodeBlock(CodeBlock&&) = delete;
    CodeBlock& operator=(CodeBlock&&) = delete;

    /// Pointer into the executable (RX) view. Generated code runs from here.
    std::uint32_t* ptr() const
    {
        return m_memory;
    }

    /// Pointer into the writable (RW) view. Code must be written through here;
    /// in single-map mode it is the same pointer as `ptr()`.
    std::uint32_t* wptr() const
    {
        return m_wmemory;
    }

    /// Offset from the executable view to the writable alias (0 in single-map mode).
    std::ptrdiff_t woffset() const
    {
        return m_dual_map ? (std::ptrdiff_t)(m_wmemory - m_memory) : 0;
    }

    bool is_dual_map() const
    {
        return m_dual_map;
    }

    void protect()
    {
        if (m_dual_map)
            return;

        if (m_protect_mode == ProtectMode::MapJit) {
            jit_toggle(true);
            return;
        }

#if defined(__APPLE__) && !TARGET_OS_IPHONE
        pthread_jit_write_protect_np(1);
#elif defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
        mprotect(m_memory, m_size, PROT_READ | PROT_EXEC);
#endif
    }

    void unprotect()
    {
        if (m_dual_map)
            return;

        if (m_protect_mode == ProtectMode::MapJit) {
            jit_toggle(false);
            return;
        }

#if defined(__APPLE__) && !TARGET_OS_IPHONE
        pthread_jit_write_protect_np(0);
#elif defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
        mprotect(m_memory, m_size, PROT_READ | PROT_WRITE);
#endif
    }

    /// Per-thread write toggle for a MAP_JIT mapping (the classic
    /// Apple-Silicon JIT dance; on iOS the symbol is internal, so it is
    /// resolved with dlsym, like the ARMSX2 port does).
    static void jit_toggle(bool protect)
    {
#if defined(__APPLE__)
#    if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        static void (*toggle)(int) = reinterpret_cast<void (*)(int)>(
            dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"));
        if (toggle)
            toggle(protect ? 1 : 0);
#    else
        pthread_jit_write_protect_np(protect ? 1 : 0);
#    endif
#endif
    }

    /// Invalidate the caches for `size` bytes starting at `mem`.
    /// `mem` must be a pointer into the executable view; in dual-map mode
    /// the data side of the flush is performed on the writable alias.
    void invalidate(std::uint32_t* mem, std::size_t size)
    {
#if defined(__APPLE__)
        if (m_dual_map) {
            flush_dual(mem, size);
            return;
        }
        sys_icache_invalidate(mem, size);
#elif defined(_WIN32)
        FlushInstructionCache(GetCurrentProcess(), mem, size);
#else
        if (m_dual_map) {
            flush_dual(mem, size);
            return;
        }
        static std::size_t icache_line_size = 0x10000, dcache_line_size = 0x10000;

        std::uint64_t ctr;
        __asm__ volatile("mrs %0, ctr_el0"
                         : "=r"(ctr));

        const std::size_t isize = icache_line_size = std::min<std::size_t>(icache_line_size, 4 << ((ctr >> 0) & 0xf));
        const std::size_t dsize = dcache_line_size = std::min<std::size_t>(dcache_line_size, 4 << ((ctr >> 16) & 0xf));

        const std::uintptr_t end = (std::uintptr_t)mem + size;

        for (std::uintptr_t addr = ((std::uintptr_t)mem) & ~(dsize - 1); addr < end; addr += dsize) {
            __asm__ volatile("dc cvau, %0"
                             :
                             : "r"(addr)
                             : "memory");
        }
        __asm__ volatile("dsb ish\n"
                         :
                         :
                         : "memory");

        for (std::uintptr_t addr = ((std::uintptr_t)mem) & ~(isize - 1); addr < end; addr += isize) {
            __asm__ volatile("ic ivau, %0"
                             :
                             : "r"(addr)
                             : "memory");
        }
        __asm__ volatile("dsb ish\nisb\n"
                         :
                         :
                         : "memory");
#endif
    }

    void invalidate_all()
    {
        invalidate(m_memory, m_size);
    }

protected:
    /// Publish stores through RW before invalidating instruction fetches
    /// through RX. I-cache and D-cache line sizes need not be equal: a
    /// fixed 128-byte stride can skip 64-byte I-cache lines. Darwin's cache
    /// APIs handle the current CPU's sizes and the required barriers.
    void flush_dual(std::uint32_t* rx_mem, std::size_t size) const
    {
        if (size == 0)
            return;
        const std::uintptr_t rw_start = (std::uintptr_t)(m_wmemory + (rx_mem - m_memory));
#if defined(__APPLE__)
        sys_dcache_flush(reinterpret_cast<void*>(rw_start), size);
        sys_icache_invalidate(rx_mem, size);
#elif defined(__aarch64__)
        std::uint64_t ctr;
        __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
        const std::size_t dsize = 4u << ((ctr >> 16) & 0xf);
        const std::size_t isize = 4u << (ctr & 0xf);
        const std::uintptr_t rw_end = rw_start + size;
        const std::uintptr_t rx_start = (std::uintptr_t)rx_mem;
        const std::uintptr_t rx_end = rx_start + size;
        for (std::uintptr_t addr = rw_start & ~(dsize - 1); addr < rw_end; addr += dsize) {
            __asm__ volatile("dc cvau, %0" :: "r"(addr) : "memory");
        }
        __asm__ volatile("dsb ish" ::: "memory");
        for (std::uintptr_t addr = rx_start & ~(isize - 1); addr < rx_end; addr += isize) {
            __asm__ volatile("ic ivau, %0" :: "r"(addr) : "memory");
        }
        __asm__ volatile("dsb ish\nisb" ::: "memory");
#else
        (void)rw_start;
        (void)rx_mem;
        (void)size;
#endif
    }

    std::uint32_t* m_memory = nullptr;  // executable view (owned base in single-map mode)
    std::uint32_t* m_wmemory = nullptr; // writable view (== m_memory in single-map mode)
    std::size_t m_size = 0;
    bool m_dual_map = false;
    ProtectMode m_protect_mode = ProtectMode::Mprotect;
    bool m_owned = false;
};

}  // namespace oaknut
