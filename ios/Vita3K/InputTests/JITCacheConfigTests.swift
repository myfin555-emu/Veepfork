import XCTest
@testable import Vita3K

@MainActor
final class JITCacheConfigTests: XCTestCase {
    func testCacheSizesPersistPerGameAndPreserveOtherSettings() throws {
        let documents = try XCTUnwrap(FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        let title = "JIT" + UUID().uuidString.prefix(6)
        let other = "JIT" + UUID().uuidString.prefix(6)
        defer { _ = Vita3KCore.gameConfigReset(appPath: title) }
        let defaults = Vita3KCore.gameConfigGet(appPath: other)
        let inheritedCache = try XCTUnwrap(defaults.split(separator: "\n").first { $0.hasPrefix("cpu_jit_cache_mib=") })
        let file = documents.appendingPathComponent("config/config/config_\(title).xml")
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        // A previously applied compatibility fix must remain visible and
        // survive changes to unrelated settings.
        try "<config><cpu cpu-opt=\"true\" cpu-jit-cache-mib=\"8\" cpu-jit-arena-mib=\"512\" /></config>"
            .write(to: file, atomically: true, encoding: .utf8)
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("cpu_jit_cache_mib=8\n"))
        XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "fps_hack", value: "1"), 0)
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("cpu_jit_cache_mib=8\n"))
        for size in [8, 12, 16, 20, 24, 28, 32, 0] {
            XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "cpu_jit_cache_mib", value: String(size)), 0)
            let report = Vita3KCore.gameConfigGet(appPath: title)
            XCTAssertTrue(report.contains("cpu_jit_cache_mib=\(size)\n"), report)
            XCTAssertTrue(report.contains("fps_hack=1\n"))
            let xml = try String(contentsOf: file, encoding: .utf8)
            XCTAssertTrue(xml.contains("cpu-jit-cache-mib=\"\(size)\""), xml)
            XCTAssertTrue(xml.contains("cpu-jit-arena-mib=\"512\""), xml)
            XCTAssertEqual(Vita3KCore.gameConfigGet(appPath: other), defaults)
        }
        XCTAssertEqual(Vita3KCore.gameConfigReset(appPath: title), 0)
        XCTAssertFalse(FileManager.default.fileExists(atPath: file.path))
        let reset = Vita3KCore.gameConfigGet(appPath: title)
        XCTAssertTrue(reset.contains(String(inheritedCache) + "\n"))
        XCTAssertTrue(reset.contains("has_custom=0\n"))
    }

    func testInvalidCacheSizesDoNotCreateOrOverwriteCustomConfig() throws {
        let documents = try XCTUnwrap(FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        let title = "JIT" + UUID().uuidString.prefix(6)
        defer { _ = Vita3KCore.gameConfigReset(appPath: title) }
        let file = documents.appendingPathComponent("config/config/config_\(title).xml")
        XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "cpu_jit_cache_mib", value: "4"), -2)
        XCTAssertFalse(FileManager.default.fileExists(atPath: file.path))
        XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "cpu_jit_cache_mib", value: "12"), 0)
        let before = try Data(contentsOf: file)
        for invalid in ["", "4", "7", "9", "10", "33", "36", "64", "-8", "8.0", "8MiB", "8garbage", " 8", "+8", "08", "999999999999999999999"] {
            XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "cpu_jit_cache_mib", value: invalid), -2, invalid)
            XCTAssertEqual(try Data(contentsOf: file), before, invalid)
        }
    }

    func testAutoUsesCoreTitleProfilesAndSurvivesOtherSettingChanges() throws {
        let documents = try XCTUnwrap(FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        // Preserve any real per-title config in this simulator while testing
        // the same profiles and resolver used by the launch path.
        for (title, expected) in [("PCSE00892", 8), ("PCSE00398", 8), ("JITAUTO01", 16), ("PCSE008920", 16)] {
            let file = documents.appendingPathComponent("config/config/config_\(title).xml")
            let previous = try? Data(contentsOf: file)
            defer {
                if let previous { try? previous.write(to: file, options: .atomic) }
                else { _ = Vita3KCore.gameConfigReset(appPath: title) }
            }
            _ = Vita3KCore.gameConfigReset(appPath: title)
            let inherited = Vita3KCore.gameConfigGet(appPath: title)
            XCTAssertTrue(inherited.contains("cpu_jit_cache_mib=0\n"), inherited)
            XCTAssertTrue(inherited.contains("cpu_jit_cache_auto_mib=\(expected)\n"), inherited)
            XCTAssertFalse(FileManager.default.fileExists(atPath: file.path))
            try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try "<config><cpu cpu-opt=\"true\" /></config>".write(to: file, atomically: true, encoding: .utf8)
            XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("cpu_jit_cache_mib=0\n"))
            XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "cpu_jit_cache_mib", value: "24"), 0)
            XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("cpu_jit_cache_mib=24\n"))
            XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "cpu_jit_cache_mib", value: "0"), 0)
            XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "fps_hack", value: "1"), 0)
            let automatic = Vita3KCore.gameConfigGet(appPath: title)
            XCTAssertTrue(automatic.contains("cpu_jit_cache_mib=0\n"), automatic)
            XCTAssertTrue(automatic.contains("cpu_jit_cache_auto_mib=\(expected)\n"), automatic)
            let xml = try String(contentsOf: file, encoding: .utf8)
            XCTAssertTrue(xml.contains("cpu-jit-cache-mib=\"0\""), xml)
            XCTAssertEqual(Vita3KCore.gameConfigReset(appPath: title), 0)
            XCTAssertEqual(Vita3KCore.gameConfigGet(appPath: title), inherited)
        }
    }
}
