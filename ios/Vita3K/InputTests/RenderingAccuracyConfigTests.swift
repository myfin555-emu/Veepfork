import XCTest
@testable import Vita3K

@MainActor
final class RenderingAccuracyConfigTests: XCTestCase {
    func testKillzoneDefaultsAllowOverridesAndResetWithoutAffectingOtherGames() throws {
        let documents = try XCTUnwrap(FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        let other = "ACC" + UUID().uuidString.prefix(6)
        let otherDefaults = Vita3KCore.gameConfigGet(appPath: other)
        XCTAssertTrue(otherDefaults.contains("high_accuracy=0\n"), otherDefaults)

        // Exercise the same resolver used at launch, preserving any real
        // per-game settings already present in this simulator.
        for title in ["PCSA00107", "PCSF00403"] {
            let file = documents.appendingPathComponent("config/config/config_\(title).xml")
            let previous = try? Data(contentsOf: file)
            defer {
                if let previous { try? previous.write(to: file, options: .atomic) }
                else { _ = Vita3KCore.gameConfigReset(appPath: title) }
            }
            XCTAssertEqual(Vita3KCore.gameConfigReset(appPath: title), 0)
            let defaults = Vita3KCore.gameConfigGet(appPath: title)
            XCTAssertTrue(defaults.contains("high_accuracy=1\n"), defaults)
            XCTAssertTrue(defaults.contains("has_custom=0\n"), defaults)
            XCTAssertFalse(FileManager.default.fileExists(atPath: file.path))

            XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "cpu_jit_cache_mib", value: "24"), 0)
            XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("high_accuracy=1\n"))
            for accuracy in [0, 1, 0] {
                XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "high_accuracy", value: String(accuracy)), 0)
                XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "fps_hack", value: "1"), 0)
                let report = Vita3KCore.gameConfigGet(appPath: title)
                XCTAssertTrue(report.contains("high_accuracy=\(accuracy)\n"), report)
                XCTAssertTrue(report.contains("cpu_jit_cache_mib=24\n"), report)
                XCTAssertTrue(report.contains("fps_hack=1\n"), report)
                let xml = try String(contentsOf: file, encoding: .utf8)
                XCTAssertTrue(xml.contains("high-accuracy=\"\(accuracy == 1 ? "true" : "false")\""), xml)
                XCTAssertEqual(Vita3KCore.gameConfigGet(appPath: other), otherDefaults)
            }
            XCTAssertEqual(Vita3KCore.gameConfigReset(appPath: title), 0)
            XCTAssertEqual(Vita3KCore.gameConfigGet(appPath: title), defaults)
            XCTAssertFalse(FileManager.default.fileExists(atPath: file.path))
        }
    }

    func testMissingXMLAttributeInheritsOnlyExactKillzoneTitleProfiles() throws {
        let documents = try XCTUnwrap(FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        for (title, expected) in [("PCSA00107", 1), ("PCSF00403", 1), ("PCSA001070", 0), ("PCSF004030", 0)] {
            let file = documents.appendingPathComponent("config/config/config_\(title).xml")
            let previous = try? Data(contentsOf: file)
            defer {
                if let previous { try? previous.write(to: file, options: .atomic) }
                else { _ = Vita3KCore.gameConfigReset(appPath: title) }
            }
            _ = Vita3KCore.gameConfigReset(appPath: title)
            XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("high_accuracy=\(expected)\n"))
            try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            for xml in ["<config><cpu cpu-opt=\"true\" /></config>",
                        "<config><gpu backend-renderer=\"Vulkan\" resolution-multiplier=\"1\" /></config>"] {
                try xml.write(to: file, atomically: true, encoding: .utf8)
                let report = Vita3KCore.gameConfigGet(appPath: title)
                XCTAssertTrue(report.contains("high_accuracy=\(expected)\n"), report)
                // Reading defaults must not turn them into saved overrides.
                XCTAssertEqual(try String(contentsOf: file, encoding: .utf8), xml)
                XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "shader_cache", value: "1"), 0)
                XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("high_accuracy=\(expected)\n"))
                XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "high_accuracy", value: expected == 1 ? "0" : "1"), 0)
                XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("high_accuracy=\(1 - expected)\n"))
            }
        }
    }
}
