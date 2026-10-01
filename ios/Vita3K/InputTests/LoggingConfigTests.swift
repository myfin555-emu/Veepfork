import XCTest
@testable import Vita3K

@MainActor
final class LoggingConfigTests: XCTestCase {
    func testLoggingOptInIsPersistedPerTitleAndResetToOff() throws {
        let documents = try XCTUnwrap(FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        let first = "LOGTEST01"
        let second = "LOGTEST02"
        defer {
            _ = Vita3KCore.gameConfigReset(appPath: first)
            _ = Vita3KCore.gameConfigReset(appPath: second)
        }
        _ = Vita3KCore.gameConfigReset(appPath: first)
        _ = Vita3KCore.gameConfigReset(appPath: second)
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: first).contains("log_level=6\n"))
        XCTAssertFalse(Vita3KCore.loggingEnabled)
        XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: first, key: "log_level", value: "1"), 0)
        XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: first, key: "log_compat_warn", value: "1"), 0)
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: first).contains("log_level=1\n"))
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: first).contains("log_compat_warn=1\n"))
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: second).contains("log_level=6\n"))
        // Editing a title in the library must not turn on process-wide logging.
        XCTAssertFalse(Vita3KCore.loggingEnabled)
        for invalid in ["", "abc", "-1", "7", "1garbage"] {
            XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: first, key: "log_level", value: invalid), -2)
        }
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: first).contains("log_level=1\n"))
        XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: first, key: "log_level", value: "6"), 0)
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: first).contains("log_level=6\n"))
        XCTAssertEqual(Vita3KCore.gameConfigReset(appPath: first), 0)
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: first).contains("log_compat_warn=0\n"))
    }

    func testOldPartialConfigKeepsCpuSettingsAndDefaultsLoggingToOff() throws {
        let documents = try XCTUnwrap(FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        let title = "LOGTEST03"
        let file = documents.appendingPathComponent("config/config/config_\(title).xml")
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try "<config><cpu cpu-opt=\"true\" cpu-jit-cache-mib=\"8\" cpu-jit-arena-mib=\"512\" /></config>".write(to: file, atomically: true, encoding: .utf8)
        defer { _ = Vita3KCore.gameConfigReset(appPath: title) }
        XCTAssertTrue(Vita3KCore.gameConfigGet(appPath: title).contains("log_level=6\n"))
        XCTAssertEqual(Vita3KCore.gameConfigSet(appPath: title, key: "log_level", value: "0"), 0)
        let xml = try String(contentsOf: file, encoding: .utf8)
        XCTAssertTrue(xml.contains("cpu-jit-cache-mib=\"8\""), xml)
        XCTAssertTrue(xml.contains("cpu-jit-arena-mib=\"512\""), xml)
        XCTAssertTrue(xml.contains("ios-log-level=\"0\""), xml)
    }

    func testDisabledSwiftProbeDoesNotEvaluateItsMessage() {
        XCTAssertFalse(Vita3KCore.loggingEnabled)
        var evaluated = false
        func message() -> String { evaluated = true; return "disabled probe" }
        Probe.mark(message())
        XCTAssertFalse(evaluated)
    }
}
