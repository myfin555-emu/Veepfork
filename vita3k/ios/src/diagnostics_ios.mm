// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <ios/diagnostics.h>
#include <Foundation/Foundation.h>
#include <fmt/format.h>
#include <mach/mach.h>
#include <sys/resource.h>

namespace vita::ios {
std::string diagnostic_host_snapshot() {
    @autoreleasepool {
        task_vm_info_data_t vm{};
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        const auto memory_status = task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count);
        struct rusage usage{};
        const auto cpu_status = getrusage(RUSAGE_SELF, &usage);
        const auto cpu_us = (uint64_t(usage.ru_utime.tv_sec) + usage.ru_stime.tv_sec) * 1000000 + usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
        const auto process = [NSProcessInfo processInfo];
        return fmt::format("footprint_bytes={} memory_status={} process_cpu_us={} cpu_status={} thermal={} low_power={}",
            vm.phys_footprint, memory_status, cpu_us, cpu_status,
            static_cast<long>(process.thermalState), bool(process.lowPowerModeEnabled));
    }
}
}
