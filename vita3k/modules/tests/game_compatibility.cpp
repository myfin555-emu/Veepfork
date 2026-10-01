// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <codec/state.h>
#include <dialog/state.h>
#include <emuenv/state.h>
#include <gtest/gtest.h>
#include <io/functions.h>
#include <io/state.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <mem/functions.h>
#include <module/module.h>
#include <ngs/modules/compressor.h>
#include <ngs/modules/player.h>
#include <np/functions.h>
#include <np/state.h>
#include <renderer/gxm_types.h>
#include <util/string_utils.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <thread>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

DECL_EXPORT(int, sceGxmColorSurfaceInit, SceGxmColorSurface *, SceGxmColorFormat, SceGxmColorSurfaceType, SceGxmColorSurfaceScaleMode, SceGxmOutputRegisterSize, uint32_t, uint32_t, uint32_t, Ptr<void>);
DECL_EXPORT(void, sceGxmColorSurfaceGetClip, const SceGxmColorSurface *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
DECL_EXPORT(void, sceGxmColorSurfaceSetClip, SceGxmColorSurface *, uint32_t, uint32_t, uint32_t, uint32_t);
DECL_EXPORT(int, sceSaveDataDialogFinish, const SceSaveDataDialogFinishParam *);
DECL_EXPORT(int, sceSaveDataDialogGetStatus);
DECL_EXPORT(int, sceSaveDataDialogTerm);
DECL_EXPORT(int, sceLocationInit, SceUInt32, SceUInt32);
DECL_EXPORT(int, sceLocationOpen, Ptr<SceInt32>, SceUInt32, SceUInt32);
DECL_EXPORT(int, sceLocationGetMethod, SceInt32, Ptr<SceUInt32>, Ptr<SceUInt32>);
DECL_EXPORT(int, sceLocationClose, SceInt32);
DECL_EXPORT(int, sceNpMatching2CreateContext, Ptr<void>, Ptr<void>, Ptr<void>, SceUInt16 *);
DECL_EXPORT(int, sceNpMatching2RegisterContextCallback, Ptr<void>, Ptr<void>);
DECL_EXPORT(int, sceNpMatching2ContextStart, SceUInt32, SceUInt32, SceUInt32, SceUInt32);
DECL_EXPORT(int, _sceKernelWaitThreadEnd, SceUID, int *, SceUInt *);
DECL_EXPORT(SceInt32, _sceKernelGetThreadInfo, SceUID, Ptr<SceKernelThreadInfo>);
extern const LibraryInitFn import_library_init_SceLibLocation;

namespace {
class GameCompatibility : public testing::Test {
protected:
    EmuEnvState emuenv;
    const SceUID thread_id = 1;

    void SetUp() override {
        ASSERT_TRUE(init(emuenv.mem, false));
    }

    void TearDown() override {
        emuenv.kernel.threads.clear();
        deinit_mem(emuenv.mem);
    }

    ThreadStatePtr thread(SceUID id) {
        auto result = std::make_shared<ThreadState>(id, emuenv.kernel, emuenv.mem);
        result->name = "compatibility-test";
        result->entry_point = 0;
        result->stack_size = 0;
        result->priority = 160;
        result->affinity_mask = 4 << 16;
        emuenv.kernel.threads.emplace(id, result);
        return result;
    }
};
} // namespace

TEST_F(GameCompatibility, injustice_clip_round_trip_preserves_surface) {
    SceGxmColorSurface surface{};
    ASSERT_EQ(CALL_EXPORT(sceGxmColorSurfaceInit, &surface, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
                  SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                  SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, 960, 544, 960, Ptr<void>(0x10000)),
        0);
    uint32_t x0 = 99, y0 = 99, x1 = 99, y1 = 99;
    CALL_EXPORT(sceGxmColorSurfaceGetClip, &surface, &x0, &y0, &x1, &y1);
    EXPECT_EQ(x0, 0);
    EXPECT_EQ(y0, 0);
    EXPECT_EQ(x1, 959);
    EXPECT_EQ(y1, 543);
    CALL_EXPORT(sceGxmColorSurfaceSetClip, &surface, 3, 7, 831, 479);
    CALL_EXPORT(sceGxmColorSurfaceGetClip, &surface, &x0, &y0, &x1, &y1);
    EXPECT_EQ(x0, 3);
    EXPECT_EQ(y0, 7);
    EXPECT_EQ(x1, 831);
    EXPECT_EQ(y1, 479);
    CALL_EXPORT(sceGxmColorSurfaceGetClip, &surface, nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(surface.width, 960);
    EXPECT_EQ(surface.height, 544);
    EXPECT_EQ(surface.data.address(), 0x10000);
}

TEST_F(GameCompatibility, savedata_finish_remains_pollable_until_term) {
    auto &dialog = emuenv.common_dialog;
    dialog.type = SAVEDATA_DIALOG;
    dialog.status = SCE_COMMON_DIALOG_STATUS_RUNNING;
    dialog.substatus = SCE_COMMON_DIALOG_STATUS_FINISHED;
    SceSaveDataDialogFinishParam param{};
    ASSERT_EQ(CALL_EXPORT(sceSaveDataDialogFinish, &param), 0);
    ASSERT_TRUE(dialog.savedata.finishing);
    dialog.savedata.finish_tick = UINT64_MAX;
    EXPECT_EQ(CALL_EXPORT(sceSaveDataDialogTerm), SCE_COMMON_DIALOG_ERROR_NOT_FINISHED);
    EXPECT_EQ(CALL_EXPORT(sceSaveDataDialogGetStatus), SCE_COMMON_DIALOG_STATUS_RUNNING);
    dialog.savedata.finish_tick = 0;
    EXPECT_EQ(CALL_EXPORT(sceSaveDataDialogGetStatus), SCE_COMMON_DIALOG_STATUS_FINISHED);
    EXPECT_FALSE(dialog.savedata.finishing);
    // Demon Gaze / Disgaea poll again before Term: FINISHED must not become NONE.
    EXPECT_EQ(CALL_EXPORT(sceSaveDataDialogGetStatus), SCE_COMMON_DIALOG_STATUS_FINISHED);
    EXPECT_EQ(CALL_EXPORT(sceSaveDataDialogTerm), 0);
    EXPECT_EQ(CALL_EXPORT(sceSaveDataDialogGetStatus), SCE_COMMON_DIALOG_STATUS_NONE);
}

TEST_F(GameCompatibility, jet_set_radio_named_event_can_be_opened_and_deleted) {
    const auto event = simple_event_create(emuenv.kernel, emuenv.mem, "test", "music-event", thread_id, 0, 0);
    ASSERT_GE(event, 0);
    EXPECT_EQ(simple_event_find(emuenv.kernel, "test", "music-event"), event);
    EXPECT_EQ(simple_event_find(emuenv.kernel, "test", nullptr), SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(simple_event_delete(emuenv.kernel, "test", thread_id, event), 0);
    EXPECT_EQ(simple_event_find(emuenv.kernel, "test", "music-event"), SCE_KERNEL_ERROR_UID_CANNOT_FIND_BY_NAME);
}

TEST_F(GameCompatibility, thread_wait_timeout_removes_waiter_and_preserves_result) {
    auto waiter = thread(thread_id);
    auto target = thread(2);
    waiter->status = target->status = ThreadStatus::run;
    int result = 123;
    SceUInt timeout = 1000;
    EXPECT_EQ(CALL_EXPORT(_sceKernelWaitThreadEnd, 2, &result, &timeout), SCE_KERNEL_ERROR_WAIT_TIMEOUT);
    EXPECT_EQ(timeout, 0);
    EXPECT_EQ(result, 123);
    EXPECT_TRUE(target->waiting_threads.empty());
    EXPECT_EQ(waiter->status, ThreadStatus::run);
    target->status = ThreadStatus::dormant;
    target->returned_value = 42;
    EXPECT_EQ(CALL_EXPORT(_sceKernelWaitThreadEnd, 2, &result, nullptr), 0);
    EXPECT_EQ(result, 42);
    EXPECT_EQ(CALL_EXPORT(_sceKernelWaitThreadEnd, thread_id, &result, nullptr), SCE_KERNEL_ERROR_ILLEGAL_THREAD_ID);
}

TEST_F(GameCompatibility, dragon_quest_thread_info_initializes_all_fields) {
    auto target = thread(thread_id);
    target->returned_value = 17;
    const Ptr<SceKernelThreadInfo> info(alloc(emuenv.mem, sizeof(SceKernelThreadInfo), "thread-info-test"));
    ASSERT_TRUE(info);
    std::memset(info.get(emuenv.mem), 0xCD, sizeof(SceKernelThreadInfo));
    info.get(emuenv.mem)->size = sizeof(SceKernelThreadInfo);
    ASSERT_EQ(CALL_EXPORT(_sceKernelGetThreadInfo, 0, info), 0);
    EXPECT_EQ(info.get(emuenv.mem)->status, SCE_KERNEL_THREAD_STATUS_DORMANT);
    EXPECT_EQ(info.get(emuenv.mem)->processId, 1);
    EXPECT_EQ(info.get(emuenv.mem)->currentCpuId, 2);
    EXPECT_EQ(info.get(emuenv.mem)->exitStatus, 17);
    EXPECT_EQ(info.get(emuenv.mem)->runClocks, 0);
    EXPECT_STREQ(info.get(emuenv.mem)->name, "compatibility-test");
}

TEST_F(GameCompatibility, signaled_condition_wait_keeps_timeout_while_relocking_mutex) {
    auto waiter = thread(thread_id);
    auto producer = thread(2);
    waiter->status = producer->status = ThreadStatus::run;

    for (const auto weight : { SyncWeight::Light, SyncWeight::Heavy }) {
        SCOPED_TRACE(static_cast<int>(weight));
        const char *export_name = "condition-relock-test";
        const Ptr<SceKernelLwMutexWork> work(alloc(emuenv.mem, sizeof(SceKernelLwMutexWork), "condition-mutex"));
        SceUID mutex_id = 0, cond_id = 0;
        ASSERT_EQ(mutex_create(&mutex_id, emuenv.kernel, emuenv.mem, export_name, "DrawSignal", thread_id,
                      0, 1, work, weight), SCE_KERNEL_OK);
        ASSERT_EQ(condvar_create(&cond_id, emuenv.kernel, export_name, "DrawSignal", thread_id,
                      0, mutex_id, weight), SCE_KERNEL_OK);
        auto mutex = mutex_get(emuenv.kernel, export_name, thread_id, mutex_id, weight);
        auto cond = (weight == SyncWeight::Light ? emuenv.kernel.lwcondvars : emuenv.kernel.condvars).at(cond_id);
        SceUInt timeout = 200000;
        auto result = std::async(std::launch::async, [&] {
            return condvar_wait(emuenv.kernel, emuenv.mem, export_name, thread_id, cond_id, &timeout, weight);
        });

        // Wait until the consumer releases the guest mutex and queues its wait.
        bool queued = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                const std::lock_guard<std::mutex> lock(cond->mutex);
                queued = !cond->waiting_threads->empty();
            }
            if (queued || result.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EXPECT_TRUE(queued);
        if (!queued) {
            result.get();
            continue;
        }

        ASSERT_EQ(mutex_lock(emuenv.kernel, emuenv.mem, export_name, producer->id, mutex_id, 1, nullptr, weight), SCE_KERNEL_OK);
        EXPECT_EQ(condvar_signal(emuenv.kernel, export_name, producer->id, cond_id,
                      Condvar::SignalTarget(Condvar::SignalTarget::Type::Any), weight), SCE_KERNEL_OK);

        // Like Ys VIII's loader, the producer retains the mutex while waiting
        // for the consumer to finish. A signal must not discard its timeout.
        const bool returned_before_unlock = result.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        {
            const std::lock_guard<std::mutex> lock(mutex->mutex);
            EXPECT_EQ(mutex->owner, producer);
            EXPECT_EQ(mutex->lock_count, 1);
            if (returned_before_unlock)
                EXPECT_TRUE(mutex->waiting_threads->empty());
        }
        // Release even on a regression so the test reports failure, not a hang.
        EXPECT_EQ(mutex_unlock(emuenv.kernel, export_name, producer->id, mutex_id, 1, weight), SCE_KERNEL_OK);
        EXPECT_TRUE(returned_before_unlock);
        EXPECT_EQ(result.get(), SCE_KERNEL_ERROR_WAIT_TIMEOUT);
        EXPECT_EQ(timeout, 0);
    }
}

TEST_F(GameCompatibility, expired_condition_wait_recovers_an_available_mutex) {
    auto waiter = thread(thread_id);
    waiter->status = ThreadStatus::run;
    for (const auto weight : { SyncWeight::Light, SyncWeight::Heavy }) {
        SCOPED_TRACE(static_cast<int>(weight));
        const char *export_name = "condition-timeout-test";
        const Ptr<SceKernelLwMutexWork> work(alloc(emuenv.mem, sizeof(SceKernelLwMutexWork), "condition-mutex"));
        SceUID mutex_id = 0, cond_id = 0;
        ASSERT_EQ(mutex_create(&mutex_id, emuenv.kernel, emuenv.mem, export_name, "condition", thread_id,
                      0, 1, work, weight), SCE_KERNEL_OK);
        ASSERT_EQ(condvar_create(&cond_id, emuenv.kernel, export_name, "condition", thread_id,
                      0, mutex_id, weight), SCE_KERNEL_OK);
        SceUInt timeout = 0;
        EXPECT_EQ(condvar_wait(emuenv.kernel, emuenv.mem, export_name, thread_id, cond_id, &timeout, weight),
            SCE_KERNEL_ERROR_WAIT_TIMEOUT);
        auto mutex = mutex_get(emuenv.kernel, export_name, thread_id, mutex_id, weight);
        EXPECT_EQ(mutex->owner, waiter);
        EXPECT_EQ(mutex->lock_count, 1);
        EXPECT_EQ(waiter->status, ThreadStatus::run);
        EXPECT_EQ(timeout, 0);
        if (weight == SyncWeight::Light) {
            EXPECT_EQ(work.get(emuenv.mem)->owner, thread_id);
            EXPECT_EQ(work.get(emuenv.mem)->lockCount, 1);
        }
        EXPECT_EQ(mutex_unlock(emuenv.kernel, export_name, thread_id, mutex_id, 1, weight), SCE_KERNEL_OK);
    }
}

TEST_F(GameCompatibility, disgaea_location_handles_have_initialized_state) {
    import_library_init_SceLibLocation(emuenv);
    ASSERT_EQ(CALL_EXPORT(sceLocationInit, 0, 0), 0);
    const Ptr<SceInt32> handle(alloc(emuenv.mem, 16, "location-test"));
    const auto method = Ptr<SceUInt32>(handle.address() + 4);
    ASSERT_EQ(CALL_EXPORT(sceLocationOpen, handle, 3, 0), 0);
    const int id = *handle.get(emuenv.mem);
    ASSERT_GT(id, 0);
    ASSERT_EQ(CALL_EXPORT(sceLocationGetMethod, id, method, Ptr<SceUInt32>()), 0);
    EXPECT_EQ(*method.get(emuenv.mem), 3);
    EXPECT_EQ(CALL_EXPORT(sceLocationClose, id), 0);
    EXPECT_LT(CALL_EXPORT(sceLocationClose, id), 0);
}

TEST_F(GameCompatibility, borderlands_offline_matching_completes_asynchronously_and_resets) {
    SceUInt16 context = 0;
    ASSERT_EQ(CALL_EXPORT(sceNpMatching2CreateContext, Ptr<void>(), Ptr<void>(), Ptr<void>(), &context), 0);
    EXPECT_GT(context, 0);
    ASSERT_EQ(CALL_EXPORT(sceNpMatching2RegisterContextCallback, Ptr<void>(0x10000), Ptr<void>(0x20000)), 0);
    EXPECT_EQ(CALL_EXPORT(sceNpMatching2ContextStart, context, 0, 0, 0), 0);
    ASSERT_EQ(emuenv.np.matching2.pending.size(), 1);
    EXPECT_EQ(emuenv.np.matching2.pending[0].ctx_id, context);
    EXPECT_NE(emuenv.np.matching2.pending[0].error_code, 0);
    deinit(emuenv.np);
    EXPECT_TRUE(emuenv.np.matching2.pending.empty());
    EXPECT_EQ(emuenv.np.matching2.context_cb_pc, 0);
    EXPECT_EQ(emuenv.np.matching2.next_ctx_id, 1);
}

TEST_F(GameCompatibility, shin_gundam_compressor_attenuates_with_stereo_link) {
    ngs::System system({}, 0);
    system.granularity = 512;
    system.sample_rate = 48000;
    ngs::Rack rack(&system, {}, 0);
    ngs::Voice voice{};
    voice.rack = &rack;
    voice.inputs.init(system.granularity, 1);
    auto *samples = reinterpret_cast<float *>(voice.inputs.inputs[0].data());
    for (int i = 0; i < system.granularity; ++i) {
        samples[2 * i] = 1.0f;
        samples[2 * i + 1] = 0.25f;
    }
    ngs::ModuleData data;
    data.parent = &voice;
    data.flags = ngs::ModuleData::PARAMS_LOCK;
    data.last_info.resize(sizeof(SceNgsCompressorParams));
    auto *params = data.get_parameters<SceNgsCompressorParams>(emuenv.mem);
    params->desc.id = SCE_NGS_COMPRESSOR_PARAMS_STRUCT_ID;
    params->fRatio = 0.25f; // hardware uses a reciprocal ratio below one
    params->fThreshold = -12.0f;
    params->fAttack = 0.0001f;
    params->fRelease = 0.001f;
    params->nStereoLink = SCE_NGS_COMPRESSOR_STEREO_LINK_ON;
    params->nPeakMode = SCE_NGS_COMPRESSOR_PEAK_MODE;
    ngs::CompressorModule compressor;
    std::unique_lock<std::recursive_mutex> scheduler_lock;
    std::unique_lock<std::mutex> voice_lock;
    EXPECT_FALSE(compressor.process(emuenv.kernel, emuenv.mem, thread_id, data, scheduler_lock, voice_lock));
    EXPECT_GT(samples[1022], 0.0f);
    EXPECT_LT(samples[1022], 0.5f);
    EXPECT_NEAR(samples[1023], samples[1022] * 0.25f, 0.00001f);
    data.is_bypassed = true;
    const float previous = samples[1022];
    compressor.process(emuenv.kernel, emuenv.mem, thread_id, data, scheduler_lock, voice_lock);
    EXPECT_EQ(samples[1022], previous);
}

TEST_F(GameCompatibility, shantae_empty_audio_chain_reaches_pcm_and_empty_cycle_terminates) {
    ngs::System system({}, 0);
    system.granularity = 4;
    system.sample_rate = 48000;
    ngs::Rack rack(&system, {}, 0);
    rack.modules.push_back(std::make_unique<ngs::PlayerModule>());
    ngs::Voice voice{};
    voice.rack = &rack;
    ngs::ModuleData data;
    data.parent = &voice;
    data.index = 0;
    data.flags = ngs::ModuleData::PARAMS_LOCK;
    data.last_info.resize(sizeof(SceNgsPlayerParams));
    auto *params = data.get_parameters<SceNgsPlayerParams>(emuenv.mem);
    params->channels = 2;
    params->playback_frequency = 48000;
    params->playback_scalar = 1.0f;
    params->buffer_params[0].next_buffer_index = 1;
    params->buffer_params[1].next_buffer_index = 2;
    params->buffer_params[2].next_buffer_index = -1;
    const Ptr<int16_t> pcm(alloc(emuenv.mem, 16, "pcm-test"));
    std::fill_n(pcm.get(emuenv.mem), 8, int16_t{ 8192 });
    new (&params->buffer_params[2]) SceNgsPlayerBufferParams{ pcm.cast<void>(), 16, 0, -1 };
    std::recursive_mutex scheduler_mutex;
    std::mutex voice_mutex;
    std::unique_lock scheduler_lock(scheduler_mutex);
    std::unique_lock voice_lock(voice_mutex);
    auto &player = *rack.modules[0];
    EXPECT_FALSE(player.process(emuenv.kernel, emuenv.mem, thread_id, data, scheduler_lock, voice_lock));
    ASSERT_NE(voice.products[0].data, nullptr);
    const auto *output = reinterpret_cast<const float *>(voice.products[0].data);
    EXPECT_NEAR(output[0], 0.25f, 0.001f);
    EXPECT_EQ(data.get_state<SceNgsPlayerStates>()->current_buffer, 2);

    data.logical_state.reset();
    data.guest_state_data.clear();
    params->buffer_params[1].next_buffer_index = 0;
    EXPECT_TRUE(player.process(emuenv.kernel, emuenv.mem, thread_id, data, scheduler_lock, voice_lock));
    data.guest_state_data.clear();
    params->buffer_params[0].next_buffer_index = 999;
    EXPECT_TRUE(player.process(emuenv.kernel, emuenv.mem, thread_id, data, scheduler_lock, voice_lock));
}

TEST_F(GameCompatibility, celcetta_missing_paths_reuse_index_and_session_reset_clears_it) {
    const auto root = fs::temp_directory_path() / fs::unique_path("vita3k-compat-%%%%-%%%%");
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            boost::system::error_code ec;
            fs::remove_all(path, ec);
        }
    } cleanup{ root };
    const fs::path translated("app/PCSG00403/MISSING.BIN");
    const auto system_path = root / "ux0" / translated;
    fs::create_directories(system_path.parent_path());
    std::ofstream((system_path.parent_path() / "DATA.BIN").string()) << "data";
    VitaIoDevice device = VitaIoDevice::app0;
    ASSERT_TRUE(find_case_isens_path(emuenv.io, device, translated, system_path));
    EXPECT_EQ(emuenv.io.indexed_roots.size(), 1);
    EXPECT_EQ(emuenv.io.cachemap.size(), 1);
    EXPECT_FALSE(find_in_cache(emuenv.io, string_utils::tolower((system_path.parent_path() / "data.bin").string())).empty());
    EXPECT_TRUE(find_in_cache(emuenv.io, string_utils::tolower(system_path.string())).empty());
    // A repeated miss must not walk this read-only game tree again.
    std::ofstream((system_path.parent_path() / "SECOND.BIN").string()) << "data";
    EXPECT_TRUE(find_case_isens_path(emuenv.io, device, translated, system_path));
    EXPECT_EQ(emuenv.io.cachemap.size(), 1);
    io_deinit(emuenv.io);
    EXPECT_TRUE(emuenv.io.indexed_roots.empty());
    EXPECT_TRUE(find_case_isens_path(emuenv.io, device, translated, system_path));
    EXPECT_EQ(emuenv.io.cachemap.size(), 2);
}

TEST(aac_compatibility, implicit_sbr_stereo_frame_fits_guest_mono_buffer) {
    AacDecoderState decoder(22050, 1, true);
    EXPECT_EQ(decoder.get(DecoderQuery::SAMPLE_RATE), 44100);
    EXPECT_EQ(decoder.get(DecoderQuery::CHANNELS), 1);
    // FFmpeg can expand a mono HE-AAC stream to stereo. Feed that decoded
    // layout into the real converter and guard the guest's 2048-sample buffer.
    auto *frame = decoder.frame;
    frame->sample_rate = 44100;
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->nb_samples = 2048;
    av_channel_layout_default(&frame->ch_layout, 2);
    ASSERT_GE(av_frame_get_buffer(frame, 0), 0);
    for (int channel = 0; channel < 2; ++channel)
        std::fill_n(reinterpret_cast<float *>(frame->extended_data[channel]), 2048, 0.25f);
    std::array<int16_t, 2064> output;
    output.fill(0x5A5A);
    DecoderSize size{};
    ASSERT_TRUE(decoder.receive(reinterpret_cast<uint8_t *>(output.data()), &size));
    EXPECT_EQ(size.samples, 2048);
    EXPECT_GT(output[0], 0);
    EXPECT_LT(output[0], 0x5A5A);
    for (size_t i = 2048; i < output.size(); ++i)
        EXPECT_EQ(output[i], 0x5A5A);

    av_frame_unref(frame);
    frame->nb_samples = 0;
    EXPECT_TRUE(decoder.receive(nullptr, &size));
    EXPECT_EQ(size.samples, 0);
}
