import Metal
import XCTest
@testable import Vita3K

final class PerformanceOverlayTests: XCTestCase {
    func testGuestFramesUseActualElapsedTimeAndStallsClearFPS() throws {
        var sampler = GuestFrameSampler()
        XCTAssertNil(sampler.sample(frames: 400, time: 10, paused: false))
        let reading = try XCTUnwrap(sampler.sample(frames: 475, time: 12.5, paused: false))
        XCTAssertEqual(reading.fps, 30, accuracy: 0.001)
        XCTAssertEqual(try XCTUnwrap(reading.frameMilliseconds), 1000 / 30, accuracy: 0.001)
        let stalled = try XCTUnwrap(sampler.sample(frames: 475, time: 13.5, paused: false))
        XCTAssertEqual(stalled.fps, 0)
        XCTAssertNil(stalled.frameMilliseconds)
    }

    func testPauseDisableAndCounterResetDiscardOldSample() throws {
        var sampler = GuestFrameSampler()
        _ = sampler.sample(frames: 60, time: 1, paused: false)
        XCTAssertNil(sampler.sample(frames: 90, time: 2, paused: true))
        XCTAssertNil(sampler.sample(frames: 90, time: 100, paused: false))
        XCTAssertEqual(try XCTUnwrap(sampler.sample(frames: 150, time: 101, paused: false)).fps, 60)
        sampler.reset()
        XCTAssertNil(sampler.sample(frames: 150, time: 200, paused: false))
        XCTAssertNil(sampler.sample(frames: 0, time: 201, paused: false))
        XCTAssertEqual(try XCTUnwrap(sampler.sample(frames: 30, time: 202, paused: false)).fps, 30)
        XCTAssertNil(sampler.sample(frames: 31, time: 202, paused: false))
    }

    func testMemoryReadsProcessFootprintAndActualMetalDevice() throws {
        let noGPU = PerformanceMemory.read(device: nil)
        XCTAssertGreaterThan(try XCTUnwrap(noGPU.footprintBytes), 0)
        XCTAssertNil(noGPU.gpuAllocatedBytes)
        let device = try XCTUnwrap(MTLCreateSystemDefaultDevice())
        let buffer = try XCTUnwrap(device.makeBuffer(length: 1_048_576, options: .storageModeShared))
        withExtendedLifetime(buffer) {
            let memory = PerformanceMemory.read(device: device)
            XCTAssertNotNil(memory.footprintBytes)
            #if targetEnvironment(simulator)
            XCTAssertNil(memory.gpuAllocatedBytes, "Simulator cannot measure Metal allocations.")
            #else
            XCTAssertGreaterThanOrEqual(memory.gpuAllocatedBytes ?? 0, UInt64(buffer.length))
            #endif
        }
    }
}
