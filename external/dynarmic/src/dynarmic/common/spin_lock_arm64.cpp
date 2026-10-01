/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <memory>
#include <mutex>

#if defined(__APPLE__)
#    include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR
#    include <sched.h>
#endif

#include <oaknut/code_block.hpp>
#include <oaknut/oaknut.hpp>

#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR
#    include <ios/jit_memory.h>
#endif

#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/common/spin_lock.h"

namespace Dynarmic {

using Backend::Arm64::Wscratch0;
using Backend::Arm64::Wscratch1;
using namespace oaknut::util;

void EmitSpinLockLock(oaknut::CodeGenerator& code, oaknut::XReg ptr) {
    oaknut::Label start, loop;

    code.MOV(Wscratch1, 1);
    code.SEVL();
    code.l(start);
    code.WFE();
    code.l(loop);
    code.LDAXR(Wscratch0, ptr);
    code.CBNZ(Wscratch0, start);
    code.STXR(Wscratch0, Wscratch1, ptr);
    code.CBNZ(Wscratch0, loop);
}

void EmitSpinLockUnlock(oaknut::CodeGenerator& code, oaknut::XReg ptr) {
    code.STLR(WZR, ptr);
}

namespace {

constexpr std::size_t kSpinLockCodeBytes = 4096;

/// Owns the emitted lock/unlock code. `mem` is heap-owned because
/// oaknut::CodeBlock is non-movable; on a physical iPhone the block is a
/// slice of the process-wide JIT arena (dual-map RX/RW, prepared
/// out-of-band by the app layer), otherwise a self-mapped CodeBlock.
struct SpinLockImpl {
    SpinLockImpl(std::unique_ptr<oaknut::CodeBlock> block, bool dual_map)
            : mem(std::move(block))
            , dual_map(dual_map)
            , code{mem->wptr(), mem->ptr()} {}

    void Initialize() {
        if (!dual_map) {
            mem->unprotect();
        }

        lock = code.xptr<void (*)(volatile int*)>();
        EmitSpinLockLock(code, X0);
        code.RET();

        unlock = code.xptr<void (*)(volatile int*)>();
        EmitSpinLockUnlock(code, X0);
        code.RET();

        if (!dual_map) {
            mem->protect();
        }
        mem->invalidate_all();
    }

    std::unique_ptr<oaknut::CodeBlock> mem;
    const bool dual_map;
    oaknut::CodeGenerator code;

    void (*lock)(volatile int*) = nullptr;
    void (*unlock)(volatile int*) = nullptr;
};

std::once_flag flag;

/// On a physical iPhone a plain PROT_READ|PROT_EXEC mapping is NOT
/// executable: TXM rejects MAP_JIT and mprotect-to-RW fails silently, so
/// code emitted into a self-mapped CodeBlock faults on the first store or
/// fetch (the KERN_PROTECTION_FAILURE crashes in DynarmicCPU::run). The
/// lock/unlock code therefore has to live in the process-wide JIT arena —
/// the same dual-map RX/RW reservation every JIT cache uses. This runs on
/// the game thread, which only starts after make_jit() confirmed the arena
/// is prepared, so `is_prepared()` is already true here.
SpinLockImpl* spin_lock_impl() {
    static std::unique_ptr<SpinLockImpl> impl;

    std::call_once(flag, [&] {
#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR
        // On a physical iPhone the lock is host code (host_spin_lock);
        // no arena slice or emitted code is needed.
        (void)impl;
#else
        // Simulator: self-mapped block; the simulator honors mmap/mprotect.
        impl = std::make_unique<SpinLockImpl>(
            std::make_unique<oaknut::CodeBlock>(kSpinLockCodeBytes), false);
        impl->Initialize();
#endif
    });

    return impl.get();
}

}  // namespace

#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR
namespace {

// Use compiled host code on a physical iPhone: a self-mapped helper is
// outside the arena registered with StikDebug. The atomic lock word is
// shared with EmitSpinLockLock/Unlock, including their acquire/release
// ordering. Yield periodically so a descheduled lock holder can run.
void host_spin_lock(volatile int* storage) {
    auto* const lock_word = const_cast<int*>(storage);
    int spins = 0;
    for (;;) {
        if (const int value = __atomic_load_n(lock_word, __ATOMIC_ACQUIRE); value == 0) {
            int expected = 0;
            if (__atomic_compare_exchange_n(lock_word, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
        }
        if (++spins % 32 == 0)
            ::sched_yield();
    }
}

void host_spin_unlock(volatile int* storage) {
    __atomic_store_n(const_cast<int*>(storage), 0, __ATOMIC_RELEASE);
}

}  // namespace
#endif

void SpinLock::Lock() {
#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR
    host_spin_lock(&storage);
#else
    spin_lock_impl()->lock(&storage);
#endif
}

void SpinLock::Unlock() {
#if defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_SIMULATOR
    host_spin_unlock(&storage);
#else
    spin_lock_impl()->unlock(&storage);
#endif
}

}  // namespace Dynarmic
