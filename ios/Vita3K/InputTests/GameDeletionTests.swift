import XCTest
@testable import Vita3K

@MainActor
final class GameDeletionTests: XCTestCase {
    private let fm = FileManager.default
    private var documents: URL!
    private var fixtures: [URL] = []

    override func setUpWithError() throws {
        documents = try XCTUnwrap(fm.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
    }

    override func tearDownWithError() throws {
        for url in fixtures.reversed() where fm.fileExists(atPath: url.path) {
            try fm.removeItem(at: url)
        }
        fixtures = []
        _ = Vita3KCore.sessionScanApps()
    }

    private func file(_ path: String) throws -> URL {
        let url = documents.appendingPathComponent(path)
        try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data("fixture".utf8).write(to: url)
        return url
    }

    private func game(_ title: String) throws -> [URL] {
        let directories = [
            "vita/ux0/app/\(title)", "vita/ux0/patch/\(title)",
            "vita/ux0/addcont/\(title)", "vita/ux0/license/\(title)",
            "cache/shaders/\(title)", "cache/shaderlog/\(title)", "logs/shaderlog/\(title)"
        ].map { documents.appendingPathComponent($0) }
        fixtures += directories
        for directory in directories {
            try fm.createDirectory(at: directory, withIntermediateDirectories: true)
            try Data("fixture".utf8).write(to: directory.appendingPathComponent("content.bin"))
        }
        let config = try file("config/config/config_\(title).xml")
        fixtures.append(config)
        XCTAssertEqual(Vita3KCore.sessionScanApps(), 0)
        return directories + [config]
    }

    func testDeleteRemovesInstalledContentAndPreferencesButKeepsUserData() async throws {
        let title = "DEL" + UUID().uuidString.prefix(6)
        let other = "DEL" + UUID().uuidString.prefix(6)
        let removed = try game(title)
        let untouched = try game(other)
        let saved = try file("vita/ux0/user/00/savedata/\(title)/save.dat")
        let trophy = try file("vita/ux0/user/00/trophy/data/\(title)/trophy.dat")
        let source = try file("imports/\(title).vpk")
        fixtures += [saved.deletingLastPathComponent(), trophy.deletingLastPathComponent(), source]

        let defaults = UserDefaults.standard
        let keys = ["library.favorites", "library.lastPlayed"]
        let previous = keys.map { defaults.object(forKey: $0) }
        defer { for (key, value) in zip(keys, previous) { defaults.set(value, forKey: key) } }
        defaults.set("\(title),\(other)", forKey: keys[0])
        defaults.set(title, forKey: keys[1])
        let controller = CoreController()
        controller.sessionInitialized = true
        controller.refreshApps()
        controller.selectedAppTitleId = title
        let app = try XCTUnwrap(controller.apps.first { $0.titleId == title })
        let error = await controller.deleteGame(app)
        XCTAssertNil(error)
        XCTAssertFalse(controller.isDeleting)
        XCTAssertNil(controller.selectedAppTitleId)
        XCTAssertFalse(controller.apps.contains { $0.titleId == title })
        XCTAssertTrue(controller.apps.contains { $0.titleId == other })
        for url in removed { XCTAssertFalse(fm.fileExists(atPath: url.path), url.path) }
        for url in untouched + [saved, trophy, source] { XCTAssertTrue(fm.fileExists(atPath: url.path), url.path) }
        XCTAssertEqual(defaults.string(forKey: keys[0]), other)
        XCTAssertNil(defaults.object(forKey: keys[1]))
        XCTAssertEqual(Vita3KCore.sessionScanApps(), 0)
        XCTAssertFalse(Vita3KCore.sessionGetApps().contains("\(title)|"))
    }

    func testInvalidAndUnknownTitlesDoNotRemoveFiles() throws {
        let title = "DEL" + UUID().uuidString.prefix(6)
        let paths = try game(title)
        for invalid in ["", ".", "..", "../\(title)", "/\(title)", "ux0/app", "ux0\\app", "UNKNOWN99"] {
            let (rc, message) = Vita3KCore.sessionDeleteApp(titleID: invalid)
            XCTAssertEqual(rc, -2, invalid)
            XCTAssertFalse(message.isEmpty)
        }
        for url in paths { XCTAssertTrue(fm.fileExists(atPath: url.path), url.path) }
    }

    func testFilesystemFailureKeepsAppVisibleAndAllowsRetry() throws {
        let title = "DEL" + UUID().uuidString.prefix(6)
        let paths = try game(title)
        let patch = paths[1]
        try fm.setAttributes([.posixPermissions: 0], ofItemAtPath: patch.path)
        defer { try? fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: patch.path) }
        let (rc, message) = Vita3KCore.sessionDeleteApp(titleID: title)
        XCTAssertEqual(rc, -4)
        XCTAssertFalse(message.isEmpty)
        XCTAssertTrue(fm.fileExists(atPath: paths[0].path))
        XCTAssertTrue(Vita3KCore.sessionGetApps().contains("\(title)|"))
        try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: patch.path)
        XCTAssertEqual(Vita3KCore.sessionDeleteApp(titleID: title).0, 0)
        XCTAssertFalse(fm.fileExists(atPath: paths[0].path))
    }

    func testLinkedAppIsRemovedWithoutDeletingItsDestination() throws {
        let title = "DEL" + UUID().uuidString.prefix(6)
        let other = "DEL" + UUID().uuidString.prefix(6)
        let paths = try game(title)
        let otherPaths = try game(other)
        try fm.removeItem(at: paths[0])
        try fm.createSymbolicLink(at: paths[0], withDestinationURL: otherPaths[0])
        XCTAssertEqual(Vita3KCore.sessionDeleteApp(titleID: title).0, 0)
        XCTAssertThrowsError(try fm.destinationOfSymbolicLink(atPath: paths[0].path))
        XCTAssertTrue(fm.fileExists(atPath: otherPaths[0].appendingPathComponent("content.bin").path))
    }

    func testControllerRefusesDeletionDuringImport() async throws {
        let title = "DEL" + UUID().uuidString.prefix(6)
        let paths = try game(title)
        let controller = CoreController()
        controller.sessionInitialized = true
        controller.isImporting = true
        let error = await controller.deleteGame(.init(titleId: title, title: title))
        XCTAssertNotNil(error)
        XCTAssertFalse(controller.isDeleting)
        for url in paths { XCTAssertTrue(fm.fileExists(atPath: url.path), url.path) }
    }
}
