// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <renderer/render_gate.h>
#include <cassert>
#include <future>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    renderer::RenderGate gate;
    std::atomic<bool> abort{ false };
    std::atomic<int> submissions{ 0 }, drains{ 0 };
    std::promise<void> in_frame, finish_frame;
    auto finish = finish_frame.get_future();
    auto rendering = std::async(std::launch::async, [&] {
        renderer::RenderGate::Binding binding(&gate, [&] { ++drains; });
        {
            auto access = gate.acquire(abort);
            ++submissions;
            in_frame.set_value();
            finish.wait(); // a frame already encoding when UIKit resigns
        }
        while (!abort.load()) {
            auto access = gate.acquire(abort);
            if (abort.load()) break;
            ++submissions;
            std::this_thread::sleep_for(1ms);
        }
    });
    in_frame.get_future().wait();
    auto suspension = std::async(std::launch::async, [&] { gate.set_suspended(true); });
    assert(suspension.wait_for(30ms) == std::future_status::timeout);
    finish_frame.set_value();
    assert(suspension.wait_for(2s) == std::future_status::ready);
    suspension.get();
    assert(drains == 1 && submissions == 1);
    std::this_thread::sleep_for(30ms);
    assert(submissions == 1); // no frames admitted while inactive
    gate.set_suspended(false);
    auto deadline = std::chrono::steady_clock::now() + 2s;
    while (submissions == 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    assert(submissions > 1); // the original render thread resumes
    gate.set_suspended(true);
    abort = true;
    assert(rendering.wait_for(2s) == std::future_status::ready);
    rendering.get(); // shutdown must not deadlock a suspended thread
    const int drained = drains;
    gate.set_suspended(true);
    assert(drains == drained); // callback cannot outlive its device

    // Background before the renderer is even created: no first-frame leak.
    abort = false;
    auto startup = std::async(std::launch::async, [&] {
        renderer::RenderGate::Binding binding(&gate, [] {});
        auto access = gate.acquire(abort);
        if (!abort.load()) ++submissions;
    });
    assert(startup.wait_for(30ms) == std::future_status::timeout);
    gate.set_suspended(false);
    assert(startup.wait_for(2s) == std::future_status::ready);
    startup.get();
    std::cout << "Render gate: drain, background, resume, startup and suspended shutdown passed\n";
}
