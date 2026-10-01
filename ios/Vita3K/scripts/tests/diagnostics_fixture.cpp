// Host-side integration fixture: the production collector, no emulator/JIT needed.
#include <util/diagnostics.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    diagnostics::initialize(argv[1], "fixture");
    if (!diagnostics::enabled()) return 0;
    diagnostics::start_sampler([] { return "paused=false thermal=0"; });
    diagnostics::begin_game("TEST00001");
    diagnostics::event("quoted", "ação\n\"value\"\\path");
    diagnostics::stage("running");
    std::vector<std::thread> producers;
    for (unsigned i = 0; i < 4; ++i) {
        producers.emplace_back([] {
            for (unsigned n = 0; n < 500; ++n) diagnostics::count(diagnostics::Metric::Pipeline, 1234);
        });
    }
    for (auto &producer : producers) producer.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    diagnostics::begin_game("TEST00002");
    diagnostics::event("before_termination", "evidence");
    if (argc > 2) {
        std::cout << "ready" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    diagnostics::stop_sampler();
}
