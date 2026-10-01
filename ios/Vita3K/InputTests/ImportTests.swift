import XCTest
@testable import Vita3K

@MainActor
final class ImportTests: XCTestCase {
    private var directory: URL!
    private let fm = FileManager.default

    override func setUpWithError() throws {
        directory = fm.temporaryDirectory.appendingPathComponent("import-tests-" + UUID().uuidString)
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try fm.removeItem(at: directory)
    }

    private func source(_ name: String = "game.zip") throws -> URL {
        let url = directory.appendingPathComponent(name)
        try Data("archive".utf8).write(to: url)
        return url
    }

    func testReceiptsSurviveReopeningAndPreventReinstall() throws {
        let url = try source()
        let fingerprint = try ImportStore.Fingerprint(url: url)
        var store = try ImportStore(directory: directory)
        XCTAssertTrue(store.shouldImport("game.zip", fingerprint: fingerprint))
        try store.record("game.zip", fingerprint: fingerprint, status: .installing)
        XCTAssertFalse(try ImportStore(directory: directory).shouldImport("game.zip", fingerprint: fingerprint))
        try store.record("game.zip", fingerprint: fingerprint, status: .succeeded)
        let reopened = try ImportStore(directory: directory)
        XCTAssertFalse(reopened.shouldImport("game.zip", fingerprint: fingerprint))
        // A retained archive must not reinstall even if timestamps change.
        try Data("changed".utf8).write(to: url)
        XCTAssertFalse(reopened.shouldImport("game.zip", fingerprint: try .init(url: url)))
    }

    func testFailedAndInterruptedImportsOnlyRetryAfterSourceChanges() throws {
        let url = try source()
        let fingerprint = try ImportStore.Fingerprint(url: url)
        var store = try ImportStore(directory: directory)
        try store.record("game.zip", fingerprint: fingerprint, status: .installing)
        var reopened = try ImportStore(directory: directory)
        XCTAssertEqual(try reopened.recoverInterrupted().count, 1)
        XCTAssertFalse(reopened.shouldImport("game.zip", fingerprint: fingerprint))
        XCTAssertTrue(try reopened.recoverInterrupted().isEmpty)
        try Data("a replacement archive".utf8).write(to: url)
        XCTAssertTrue(reopened.shouldImport("game.zip", fingerprint: try .init(url: url)))
        XCTAssertFalse(try ImportStore(directory: directory).shouldImport("game.zip", fingerprint: fingerprint))
    }

    func testLegacyMarkersAreRespectedForGamesAndAllFirmwareFiles() throws {
        let url = try source()
        let fingerprint = try ImportStore.Fingerprint(url: url)
        try "game.zip\n".write(to: directory.appendingPathComponent(".imports-done"), atomically: true, encoding: .utf8)
        try "firmware.pup\nfonts.pup\n".write(to: directory.appendingPathComponent(".fw-done"), atomically: true, encoding: .utf8)
        try "bad.zip\t\(fingerprint.modified)".write(to: directory.appendingPathComponent(".imports-failed"), atomically: true, encoding: .utf8)
        let store = try ImportStore(directory: directory)
        for name in ["game.zip", "firmware.pup", "fonts.pup", "bad.zip"] {
            XCTAssertFalse(store.shouldImport(name, fingerprint: fingerprint), name)
        }
    }

    func testJournalErrorsAreNotTreatedAsNewImports() throws {
        let journal = directory.appendingPathComponent(".imports-state.json")
        try Data("invalid json".utf8).write(to: journal)
        XCTAssertThrowsError(try ImportStore(directory: directory))
        try fm.removeItem(at: journal)
        var store = try ImportStore(directory: directory)
        let fingerprint = try ImportStore.Fingerprint(url: source())
        try fm.createDirectory(at: journal, withIntermediateDirectories: true)
        XCTAssertThrowsError(try store.record("game.zip", fingerprint: fingerprint, status: .installing))
    }

    func testSelectingStagedSourceDoesNotDeleteIt() async throws {
        let url = try source()
        let destination = directory!
        let copied = try await Task.detached { try ImportStaging.copy(url, into: destination) }.value
        XCTAssertEqual(copied, url)
        XCTAssertEqual(try Data(contentsOf: copied), Data("archive".utf8))
    }

    func testConsumedSourcesAreDeletedOnSuccessAndFailureAndCanBeImportedAgain() throws {
        for code: Int32 in [0, -3] {
            let url = try source()
            let fingerprint = try ImportStore.Fingerprint(url: url)
            let outcome = ImportStaging.consume(url, in: directory) { path in
                XCTAssertEqual(path, url.path)
                XCTAssertTrue(self.fm.fileExists(atPath: path))
                return (code, "installer report")
            }
            XCTAssertEqual(outcome.succeeded, code == 0)
            XCTAssertTrue(outcome.message.contains("installer report"))
            XCTAssertFalse(fm.fileExists(atPath: url.path))
            let reopened = try ImportStore(directory: directory)
            XCTAssertEqual(reopened.receipts[url.lastPathComponent]?.status, code == 0 ? .succeeded : .failed)
            XCTAssertTrue(reopened.shouldImport(url.lastPathComponent, fingerprint: fingerprint))
        }
    }

    func testConsumptionPreservesOriginalOutsideImportsAndRemovesStagedDirectory() throws {
        let original = directory.appendingPathComponent("original")
        let imports = directory.appendingPathComponent("imports")
        try fm.createDirectory(at: original, withIntermediateDirectories: true)
        try fm.createDirectory(at: imports, withIntermediateDirectories: true)
        let payload = original.appendingPathComponent("eboot.bin")
        try Data("original content".utf8).write(to: payload)
        let staged = try ImportStaging.copy(original, into: imports)
        let outcome = ImportStaging.consume(staged, in: imports) { _ in (0, "installed") }
        XCTAssertTrue(outcome.succeeded)
        XCTAssertFalse(fm.fileExists(atPath: staged.path))
        XCTAssertEqual(try Data(contentsOf: payload), Data("original content".utf8))

        let rejected = ImportStaging.consume(original, in: imports) { _ in
            XCTFail("Must not consume a source outside imports")
            return (0, "")
        }
        XCTAssertFalse(rejected.succeeded)
        XCTAssertTrue(fm.fileExists(atPath: payload.path))
    }

    func testSourceIsRemovedEvenWhenSavingTheFinalReceiptFails() throws {
        let url = try source()
        let journal = directory.appendingPathComponent(".imports-state.json")
        let outcome = ImportStaging.consume(url, in: directory) { _ in
            try! self.fm.removeItem(at: journal)
            try! self.fm.createDirectory(at: journal, withIntermediateDirectories: true)
            return (0, "installed before journal error")
        }
        XCTAssertFalse(outcome.succeeded)
        XCTAssertTrue(outcome.message.contains("installed before journal error"))
        XCTAssertFalse(fm.fileExists(atPath: url.path))
    }

    func testSourceIsRemovedWhenJournalCannotBeRead() throws {
        let url = try source()
        try Data("invalid json".utf8).write(to: directory.appendingPathComponent(".imports-state.json"))
        let outcome = ImportStaging.consume(url, in: directory) { _ in
            XCTFail("Must not install without a receipt")
            return (0, "")
        }
        XCTAssertFalse(outcome.succeeded)
        XCTAssertFalse(fm.fileExists(atPath: url.path))
    }

    func testSelectingImportsDirectoryRejectsRecursiveCopy() async throws {
        let source = directory!
        do {
            _ = try await Task.detached { try ImportStaging.copy(source, into: source) }.value
            XCTFail("Expected a recursive-copy error")
        } catch {}
        XCTAssertEqual(try fm.contentsOfDirectory(atPath: source.path), [])
    }

    func testFailedCopyPreservesPreviousSource() async throws {
        let existing = try source()
        let missing = directory.appendingPathComponent("missing/game.zip")
        let destination = directory!
        do {
            _ = try await Task.detached { try ImportStaging.copy(missing, into: destination) }.value
            XCTFail("Expected a copy error")
        } catch {}
        XCTAssertEqual(try Data(contentsOf: existing), Data("archive".utf8))
    }

    func testPickerFailurePublishesEveryAttemptIncludingIdenticalErrors() async throws {
        let documents = try XCTUnwrap(fm.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        let bad = try source("bad-" + UUID().uuidString + ".zip")
        let staged = URL(fileURLWithPath: Vita3KCore.sessionStagingPath()).appendingPathComponent(bad.lastPathComponent)
        defer { try? fm.removeItem(at: staged) }
        let controller = CoreController()
        controller.sessionInitialized = true
        var previousID: UUID?
        for _ in 0..<2 {
            controller.importFile(bad)
            let deadline = Date().addingTimeInterval(10)
            while controller.isImporting && Date() < deadline { try await Task.sleep(for: .milliseconds(20)) }
            XCTAssertFalse(controller.isImporting)
            let result = try XCTUnwrap(controller.importResult)
            XCTAssertFalse(result.succeeded)
            XCTAssertTrue(result.message.contains(bad.lastPathComponent))
            XCTAssertNotEqual(result.id, previousID)
            XCTAssertFalse(fm.fileExists(atPath: staged.path))
            XCTAssertTrue(fm.fileExists(atPath: bad.path))
            previousID = result.id
        }
    }

    func testExistingTitleIsPreservedByAutomaticImportRegardlessOfFilename() throws {
        let documents = try XCTUnwrap(fm.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        let title = "IMP" + UUID().uuidString.prefix(6)
        let game = directory.appendingPathComponent("An arbitrary game name")
        try fm.createDirectory(at: game.appendingPathComponent("sce_sys"), withIntermediateDirectories: true)
        func sfo(_ title: String) -> Data {
            var result = Data()
            func u32(_ value: UInt32) { var le = value.littleEndian; withUnsafeBytes(of: &le) { result.append(contentsOf: $0) } }
            func u16(_ value: UInt16) { var le = value.littleEndian; withUnsafeBytes(of: &le) { result.append(contentsOf: $0) } }
            u32(0x46535000); u32(0x101); u32(36); u32(48); u32(1)
            u16(0); u16(0x0204); u32(10); u32(10); u32(0)
            result.append(contentsOf: Array("TITLE_ID\0".utf8) + [0, 0, 0])
            result.append(contentsOf: "\(title)\0".utf8)
            return result
        }
        try sfo(title).write(to: game.appendingPathComponent("sce_sys/param.sfo"))
        try Data("first".utf8).write(to: game.appendingPathComponent("eboot.bin"))
        let destination = documents.appendingPathComponent("vita/ux0/app/\(title)")
        defer { try? fm.removeItem(at: destination); _ = Vita3KCore.sessionScanApps() }
        XCTAssertEqual(Vita3KCore.sessionInstall(stagedPath: game.path).0, 0)
        try Data("replacement".utf8).write(to: game.appendingPathComponent("eboot.bin"))
        XCTAssertEqual(Vita3KCore.sessionInstall(stagedPath: game.path, replaceExisting: false).0, 0)
        XCTAssertEqual(try Data(contentsOf: destination.appendingPathComponent("eboot.bin")), Data("first".utf8))
        XCTAssertEqual(Vita3KCore.sessionInstall(stagedPath: game.path).0, 0)
        XCTAssertEqual(try Data(contentsOf: destination.appendingPathComponent("eboot.bin")), Data("replacement".utf8))
        try Data([0]).write(to: game.appendingPathComponent("sce_sys/param.sfo"))
        let (rc, report) = Vita3KCore.sessionInstall(stagedPath: game.path)
        XCTAssertNotEqual(rc, 0)
        XCTAssertFalse(report.isEmpty)
        XCTAssertEqual(try Data(contentsOf: destination.appendingPathComponent("eboot.bin")), Data("replacement".utf8))
    }

    func testAutomaticImportFailureIsNotRepeatedByNewController() async throws {
        let documents = try XCTUnwrap(fm.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        _ = try source()
        let first = CoreController()
        await first.processStagedImports(staging: directory.path)
        XCTAssertEqual(first.importResult?.succeeded, false)
        XCTAssertFalse(fm.fileExists(atPath: directory.appendingPathComponent("game.zip").path))
        let second = CoreController()
        await second.processStagedImports(staging: directory.path, recoverInterrupted: true)
        XCTAssertNil(second.importResult)
        _ = try source()
        await second.processStagedImports(staging: directory.path)
        XCTAssertEqual(second.importResult?.succeeded, false)
        XCTAssertFalse(fm.fileExists(atPath: directory.appendingPathComponent("game.zip").path))
    }
}
