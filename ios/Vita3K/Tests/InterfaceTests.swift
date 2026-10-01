import XCTest

@MainActor
final class InterfaceTests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

    private func launch(noJIT: Bool = false, performanceOverlay: Bool? = nil,
                        forceLandscape: Bool? = false) -> XCUIApplication {
        XCUIDevice.shared.orientation = .portrait
        let app = XCUIApplication()
        app.launchArguments += ["-interface.language", "pt-BR"]
        app.launchEnvironment["VITA3K_UI_TEST"] = "1"
        app.launchEnvironment["VITA3K_FORCE_VPAD"] = "1"
        if noJIT { app.launchEnvironment["VITA3K_UI_TEST_NO_JIT"] = "1" }
        if let performanceOverlay {
            app.launchArguments += ["-emulation.showStatus", performanceOverlay ? "YES" : "NO"]
        }
        if let forceLandscape {
            app.launchArguments += ["-interface.forceLandscape", forceLandscape ? "YES" : "NO"]
        }
        app.launch()
        return app
    }

    private func capture(_ name: String) {
        let attachment = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func rotate(_ orientation: UIDeviceOrientation, app: XCUIApplication) {
        XCUIDevice.shared.orientation = orientation
        let landscape = orientation == .landscapeLeft || orientation == .landscapeRight
        waitForOrientation(landscape: landscape, app: app)
    }

    private func waitForOrientation(landscape: Bool, app: XCUIApplication) {
        let predicate = NSPredicate { _, _ in
            let frame = app.windows.firstMatch.frame
            return landscape ? frame.width > frame.height : frame.height > frame.width
        }
        expectation(for: predicate, evaluatedWith: app)
        waitForExpectations(timeout: 8)
    }

    func testForceLandscapePersistsAndRestoresAutomaticRotation() {
        let app = launch(forceLandscape: nil)
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        let toggle = app.switches["force-landscape-toggle"]
        func openOptions() {
            app.buttons["Opções"].tap()
            for _ in 0..<6 where !toggle.isHittable { app.swipeUp() }
            XCTAssertTrue(toggle.isHittable)
        }
        func flip() {
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 1, dy: 0.5))
                .withOffset(CGVector(dx: -25, dy: 0)).tap()
        }
        openOptions()
        if toggle.value as? String == "1" { flip() }
        waitForOrientation(landscape: false, app: app)
        flip()
        XCTAssertEqual(toggle.value as? String, "1")
        waitForOrientation(landscape: true, app: app)
        capture("force-landscape-settings")
        rotate(.landscapeRight, app: app)
        XCUIDevice.shared.orientation = .portrait
        waitForOrientation(landscape: true, app: app)

        XCUIDevice.shared.press(.home)
        app.activate()
        waitForOrientation(landscape: true, app: app)
        app.terminate()
        app.launch()
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        waitForOrientation(landscape: true, app: app)
        capture("force-landscape-library-relaunch")
        openOptions()
        XCTAssertEqual(toggle.value as? String, "1")
        flip()
        XCTAssertEqual(toggle.value as? String, "0")
        waitForOrientation(landscape: false, app: app)
        rotate(.landscapeLeft, app: app)
        rotate(.portrait, app: app)
        app.terminate()
        app.launch()
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        waitForOrientation(landscape: false, app: app)
        app.terminate()
    }

    func testForceLandscapeKeepsGameAndLibraryHorizontal() throws {
        let app = launch(forceLandscape: true)
        defer { app.terminate(); XCUIDevice.shared.orientation = .portrait }
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        guard app.buttons["game-XERP00001"].exists else {
            throw XCTSkip("Install XERP00001 to test forced landscape during gameplay.")
        }
        waitForOrientation(landscape: true, app: app)
        app.buttons["game-XERP00001"].tap()
        let home = app.buttons["home-button"]
        let cross = app.buttons["pad-12"]
        XCTAssertTrue(cross.waitForExistence(timeout: 60))
        rotate(.landscapeRight, app: app)
        XCUIDevice.shared.orientation = .portrait
        waitForOrientation(landscape: true, app: app)
        let surface = app.otherElements["game-surface"]
        XCTAssertGreaterThan(surface.frame.width, surface.frame.height)
        XCTAssertTrue(home.isHittable)
        XCTAssertTrue(cross.isHittable)
        XCTAssertTrue(app.windows.firstMatch.frame.contains(cross.frame))
        capture("force-landscape-game")
        home.tap()
        app.buttons["exit-game"].tap()
        XCTAssertTrue(app.buttons["import-content"].waitForExistence(timeout: 15))
        waitForOrientation(landscape: true, app: app)
    }

    // Host setup: place an invalid imports/import-regression-invalid.zip in
    // a dedicated simulator's app Documents before running this test alone.
    func testAutomaticImportFailureIsVisibleAndNotRepeatedAfterRelaunch() throws {
        guard ProcessInfo.processInfo.environment["VITA3K_IMPORT_UI_FIXTURE"] == "1" else {
            throw XCTSkip("Requires the isolated import regression fixture.")
        }
        let app = XCUIApplication()
        app.launchArguments += ["-interface.language", "pt-BR"]
        app.launch()
        let jit = app.alerts["Abra pelo StikDebug"]
        if jit.waitForExistence(timeout: 3) { jit.buttons["Continuar na biblioteca"].tap() }
        XCTAssertTrue(app.navigationBars["Falha na importação"].waitForExistence(timeout: 15))
        XCTAssertTrue(app.staticTexts.containing(NSPredicate(format: "label CONTAINS %@", "import-regression-invalid.zip")).firstMatch.exists)
        capture("import-failure")
        app.buttons["Concluir"].tap()
        app.terminate()
        app.launch()
        if jit.waitForExistence(timeout: 3) { jit.buttons["Continuar na biblioteca"].tap() }
        XCTAssertTrue(app.buttons["import-content"].waitForExistence(timeout: 15))
        XCTAssertFalse(app.navigationBars["Falha na importação"].waitForExistence(timeout: 5))
        app.terminate()
    }

    func testMissingJITPopupAndLaunchGate() {
        let app = launch(noJIT: true)
        let alert = app.alerts["Abra pelo StikDebug"]
        XCTAssertTrue(alert.waitForExistence(timeout: 15))
        capture("jit-required-portrait")
        rotate(.landscapeLeft, app: app)
        capture("jit-required-landscape")
        alert.buttons["Continuar na biblioteca"].tap()
        XCTAssertTrue(app.buttons["import-content"].isHittable)
        let game = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'game-'")).firstMatch
        if game.exists {
            game.tap()
            XCTAssertTrue(alert.waitForExistence(timeout: 10))
            XCTAssertFalse(app.buttons["exit-game"].exists)
            alert.buttons["Continuar na biblioteca"].tap()
        }
        app.buttons["Opções"].tap()
        app.buttons["check-jit"].tap()
        XCTAssertTrue(alert.waitForExistence(timeout: 10))
        app.terminate()
    }

    func testCloudSaveOptionPersistsAndCanBeDisabledWithoutJIT() {
        let app = launch(noJIT: true)
        func openOptions() {
            let alert = app.alerts["Abra pelo StikDebug"]
            XCTAssertTrue(alert.waitForExistence(timeout: 15))
            alert.buttons["Continuar na biblioteca"].tap()
            app.buttons["Opções"].tap()
            let toggle = app.switches["icloud-saves-toggle"]
            for _ in 0..<6 where !toggle.isHittable { app.swipeUp() }
            XCTAssertTrue(toggle.isHittable)
        }
        func flip() {
            let toggle = app.switches["icloud-saves-toggle"]
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 1, dy: 0.5))
                .withOffset(CGVector(dx: -25, dy: 0)).tap()
        }
        openOptions()
        let toggle = app.switches["icloud-saves-toggle"]
        XCTAssertEqual(toggle.value as? String, "0")
        flip()
        XCTAssertEqual(toggle.value as? String, "1")
        XCTAssertTrue(app.buttons["icloud-saves-sync"].waitForExistence(timeout: 5))
        app.terminate()
        app.launch()
        openOptions()
        XCTAssertEqual(toggle.value as? String, "1")
        flip()
        XCTAssertEqual(toggle.value as? String, "0")
        XCTAssertFalse(app.buttons["icloud-saves-sync"].exists)
        capture("icloud-saves-disabled")
        app.terminate()
    }

    func testLibrarySettingsAndDiagnosticsRotate() {
        let app = launch()
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        XCTAssertFalse(app.alerts["Abra pelo StikDebug"].exists)
        capture("library-portrait")
        rotate(.landscapeLeft, app: app)
        capture("library-landscape-left")
        rotate(.landscapeRight, app: app)
        XCTAssertTrue(app.buttons["import-content"].isHittable)
        app.buttons["Opções"].tap()
        XCTAssertTrue(app.switches["show-controls"].waitForExistence(timeout: 5))
        capture("settings-landscape")
        rotate(.portrait, app: app)
        capture("settings-portrait")
        let diagnostics = app.buttons["open-diagnostics"]
        for _ in 0..<6 where !diagnostics.isHittable { app.swipeUp() }
        XCTAssertTrue(diagnostics.isHittable)
        diagnostics.tap()
        XCTAssertTrue(app.navigationBars["Diagnóstico"].waitForExistence(timeout: 5))
        rotate(.landscapeLeft, app: app)
        capture("diagnostics-landscape")
        app.terminate()
    }

    func testGameConfigScreen() throws {
        let app = launch()
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        let game = app.buttons["game-XERP00001"]
        guard game.exists else { throw XCTSkip("Install XERP00001 (or PCSE00890) in this simulator to exercise the game config screen.") }
        // Long-press the game -> context menu -> "Editar Config".
        game.press(forDuration: 1.0)
        let edit = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'edit-config'")).firstMatch
        XCTAssertTrue(edit.waitForExistence(timeout: 5))
        edit.tap()
        XCTAssertTrue(app.navigationBars["Config do jogo"].waitForExistence(timeout: 10))
        capture("game-config")
        // Per-game toggles and the global controls are all present.
        XCTAssertTrue(app.switches["Hack de FPS"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.switches["Ativar otimizações da CPU"].exists)
        XCTAssertTrue(app.switches["Ativar cache de texturas"].exists)
        // A per-game change is persisted right away (no save button). The
        // row element covers the whole row; the tappable knob is a separate
        // small switch element (empty label) on the same row.
        let before = app.switches["Hack de FPS"].value as? String
        let row = app.switches["Hack de FPS"].frame
        let knob = app.switches.allElementsBoundByIndex.first { el in
            el.label.isEmpty && el.frame.width < 100
                && abs(el.frame.midY - row.midY) < row.height / 2
        }
        precondition(knob != nil, "could not locate the FPS-hack switch knob")
        knob!.tap()
        Thread.sleep(forTimeInterval: 1)
        let after = app.switches["Hack de FPS"].value as? String
        XCTAssertNotEqual(before, after, "tapping the knob must flip the toggle (before=\(before ?? "nil") after=\(after ?? "nil"))")
        capture("game-config-changed")
        // The global section is below the fold in a compact layout.
        for _ in 0..<3 where !app.switches["Registrar avisos de compatibilidade"].exists {
            app.swipeUp()
        }
        XCTAssertTrue(app.switches["Registrar avisos de compatibilidade"].waitForExistence(timeout: 5))
        app.buttons["Concluído"].tap()
        XCTAssertTrue(app.buttons["import-content"].waitForExistence(timeout: 10))
        app.terminate()
    }

    func testDeleteGameFromGridAndListWithoutJIT() throws {
        // A disposable fixture, never one of the user's installed games.
        // Create Documents/vita/ux0/app/DELETEUI1 before running this case.
        let app = launch(noJIT: true)
        let jitAlert = app.alerts["Abra pelo StikDebug"]
        XCTAssertTrue(jitAlert.waitForExistence(timeout: 15))
        jitAlert.buttons["Continuar na biblioteca"].tap()
        let game = app.buttons["game-DELETEUI1"]
        guard game.exists else { throw XCTSkip("Create the disposable DELETEUI1 fixture in the simulator.") }
        if app.buttons["Exibir em grade"].exists { app.buttons["Exibir em grade"].tap() }
        game.press(forDuration: 1)
        XCTAssertTrue(app.buttons["delete-game"].waitForExistence(timeout: 5))
        app.buttons["delete-game"].tap()
        let confirmation = app.alerts["Apagar jogo?"]
        XCTAssertTrue(confirmation.waitForExistence(timeout: 5))
        capture("delete-game-confirmation")
        confirmation.buttons["Cancelar"].tap()
        XCTAssertTrue(game.exists)
        app.buttons["Exibir em lista"].tap()
        game.press(forDuration: 1)
        XCTAssertTrue(app.buttons["delete-game"].waitForExistence(timeout: 5))
        app.buttons["delete-game"].tap()
        XCTAssertTrue(confirmation.waitForExistence(timeout: 5))
        confirmation.buttons.matching(identifier: "confirm-delete-game").firstMatch.tap()
        expectation(for: NSPredicate(format: "exists == false"), evaluatedWith: game)
        waitForExpectations(timeout: 15)
        XCTAssertFalse(app.alerts["Não foi possível apagar o jogo"].exists)
        app.terminate()
        app.launch()
        XCTAssertTrue(jitAlert.waitForExistence(timeout: 15))
        jitAlert.buttons["Continuar na biblioteca"].tap()
        XCTAssertFalse(game.exists, "The deleted game must stay absent after reopening the app.")
        app.terminate()
    }

    func testJITCachePickerPersistsPerGame() throws {
        // Provision a disposable JITCACHE1 app folder and a custom config
        // with cpu-jit-cache-mib="8" in this simulator before the test.
        let app = launch(noJIT: true)
        let alert = app.alerts["Abra pelo StikDebug"]
        XCTAssertTrue(alert.waitForExistence(timeout: 15))
        alert.buttons["Continuar na biblioteca"].tap()
        guard app.buttons["game-JITCACHE1"].exists else { throw XCTSkip("Create the disposable JITCACHE1 fixture with an 8 MiB custom config.") }
        func openConfig() {
            app.buttons["game-JITCACHE1"].press(forDuration: 1)
            app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'edit-config'")).firstMatch.tap()
            XCTAssertTrue(app.navigationBars["Config do jogo"].waitForExistence(timeout: 10))
        }
        func assertCache(_ size: Int) {
            let picker = app.buttons["game-jit-cache"]
            XCTAssertTrue(picker.waitForExistence(timeout: 5))
            let expected = size == 0 ? "Auto" : "\(size) MiB"
            XCTAssertTrue(picker.label.contains(expected) || (picker.value as? String) == expected)
        }
        openConfig()
        assertCache(8)
        app.buttons["game-jit-cache"].tap()
        XCTAssertTrue(app.buttons["Auto"].exists)
        for size in [8, 12, 16, 20, 24, 28, 32] {
            XCTAssertTrue(app.buttons["\(size) MiB"].exists)
        }
        XCTAssertFalse(app.buttons["4 MiB"].exists)
        XCTAssertFalse(app.buttons["36 MiB"].exists)
        capture("jit-cache-options")
        app.buttons["32 MiB"].tap()
        assertCache(32)
        app.buttons["Concluído"].tap()
        openConfig()
        assertCache(32)
        app.buttons["game-jit-cache"].tap()
        app.buttons["Auto"].tap()
        app.buttons["Concluído"].tap()
        app.terminate()
        app.launch()
        XCTAssertTrue(alert.waitForExistence(timeout: 15))
        alert.buttons["Continuar na biblioteca"].tap()
        openConfig()
        assertCache(0)
        capture("jit-cache-persisted")
        app.buttons["Concluído"].tap()
        app.terminate()
    }

    func testGameLoggingOptIn() throws {
        let app = launch()
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        guard app.buttons["game-XERP00001"].exists else { throw XCTSkip("Install XERP00001 to test game settings.") }
        func openConfig() {
            app.buttons["game-XERP00001"].press(forDuration: 1)
            app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH 'edit-config'")).firstMatch.tap()
            XCTAssertTrue(app.navigationBars["Config do jogo"].waitForExistence(timeout: 10))
            for _ in 0..<5 where !app.buttons["game-log-level"].isHittable { app.swipeUp() }
        }
        openConfig()
        let picker = app.buttons["game-log-level"]
        XCTAssertTrue(picker.isHittable)
        XCTAssertTrue(picker.label.contains("Desativado") || (picker.value as? String) == "Desativado")
        capture("logging-default-off")
        picker.tap()
        app.buttons["Depuração"].tap()
        app.buttons["Concluído"].tap()
        XCTAssertFalse(app.otherElements["logging-mode"].exists)
        openConfig()
        XCTAssertTrue(picker.label.contains("Depuração") || (picker.value as? String) == "Depuração")
        capture("logging-title-debug")
        picker.tap()
        app.buttons["Desativado"].tap()
        app.buttons["Concluído"].tap()
        app.terminate()
    }

    func testPerformanceOverlayLiveFramesPauseAndDisabled() throws {
        let app = launch(performanceOverlay: true)
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        guard app.buttons["game-XERP00001"].exists else { throw XCTSkip("Install XERP00001 to test live performance metrics.") }
        app.buttons["game-XERP00001"].tap()
        let overlay = app.staticTexts["performance-overlay"]
        XCTAssertTrue(overlay.waitForExistence(timeout: 30))
        let live = NSPredicate(format: "label MATCHES %@", "(?s)FPS  [1-9][0-9]*[.,][0-9].*Quadro  [0-9]+[.,][0-9] ms.*RAM  [1-9][0-9]* MiB.*GPU aloc\\..*")
        expectation(for: live, evaluatedWith: overlay)
        waitForExpectations(timeout: 20)
        capture("performance-overlay-running")
        app.buttons["home-button"].tap()
        expectation(for: NSPredicate(format: "label BEGINSWITH 'Pausado'"), evaluatedWith: overlay)
        waitForExpectations(timeout: 5)
        capture("performance-overlay-paused")
        app.buttons["resume-game"].tap()
        expectation(for: live, evaluatedWith: overlay)
        waitForExpectations(timeout: 10)
        app.buttons["home-button"].tap()
        app.buttons["exit-game"].tap()
        XCTAssertTrue(app.buttons["import-content"].waitForExistence(timeout: 15))
        XCTAssertFalse(overlay.exists)
        app.terminate()

        let disabled = launch(performanceOverlay: false)
        XCTAssertTrue(disabled.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        disabled.buttons["game-XERP00001"].tap()
        XCTAssertTrue(disabled.buttons["home-button"].waitForExistence(timeout: 30))
        XCTAssertFalse(disabled.staticTexts["performance-overlay"].exists)
        disabled.buttons["home-button"].tap()
        disabled.buttons["exit-game"].tap()
        XCTAssertTrue(disabled.buttons["import-content"].waitForExistence(timeout: 15))
        disabled.terminate()
    }

    func testHomeMenuTextResumeAndExit() throws {
        let app = launch()
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        let game = app.buttons["game-XERP00001"]
        guard game.exists else { throw XCTSkip("Install XERP00001 to test the HOME menu.") }
        game.tap()
        let home = app.buttons["home-button"]
        XCTAssertTrue(home.waitForExistence(timeout: 30))
        home.tap()
        let resume = app.buttons["resume-game"]
        let exit = app.buttons["exit-game"]
        XCTAssertTrue(resume.waitForExistence(timeout: 5))
        XCTAssertEqual(resume.label, "Continuar")
        XCTAssertEqual(exit.label, "Voltar à biblioteca")
        XCTAssertTrue(resume.isHittable)
        XCTAssertTrue(exit.isHittable)
        capture("home-menu-text")
        resume.tap()
        XCTAssertFalse(resume.exists)
        home.tap()
        XCTAssertTrue(exit.waitForExistence(timeout: 5))
        exit.tap()
        XCTAssertTrue(app.buttons["import-content"].waitForExistence(timeout: 10))
        app.terminate()
    }

    func testBackgroundRetainsGameAndUserPause() throws {
        let app = launch(performanceOverlay: true)
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        guard app.buttons["game-XERP00001"].exists else { throw XCTSkip("Install XERP00001 to test session continuity.") }
        app.buttons["game-XERP00001"].tap()
        let surface = app.otherElements["game-surface"]
        let overlay = app.staticTexts["performance-overlay"]
        XCTAssertTrue(overlay.waitForExistence(timeout: 30))

        func field(_ name: String) -> UInt64? {
            let value = surface.value as? String ?? ""
            return value.components(separatedBy: " ").first(where: { $0.hasPrefix(name + "=") })
                .flatMap { UInt64($0.dropFirst(name.count + 1)) }
        }
        func waitForFrames(after frames: UInt64) {
            expectation(for: NSPredicate { _, _ in (field("frames") ?? 0) > frames }, evaluatedWith: surface)
            waitForExpectations(timeout: 15)
        }
        func switchApps() {
            XCUIDevice.shared.press(.home)
            let settings = XCUIApplication(bundleIdentifier: "com.apple.Preferences")
            settings.activate()
            XCTAssertTrue(settings.wait(for: .runningForeground, timeout: 8))
            Thread.sleep(forTimeInterval: 2)
            app.activate()
            XCTAssertTrue(app.wait(for: .runningForeground, timeout: 8))
        }

        waitForFrames(after: 90)
        let generation = try XCTUnwrap(field("generation"))
        for _ in 0..<3 {
            let frames = try XCTUnwrap(field("frames"))
            switchApps()
            waitForFrames(after: frames)
            XCTAssertEqual(field("generation"), generation, "Returning must retain the original guest session.")
            XCTAssertFalse(app.buttons["resume-game"].exists)
        }
        app.buttons["home-button"].tap()
        XCTAssertTrue(app.buttons["resume-game"].waitForExistence(timeout: 5))
        expectation(for: NSPredicate(format: "label BEGINSWITH 'Pausado'"), evaluatedWith: overlay)
        waitForExpectations(timeout: 5)
        Thread.sleep(forTimeInterval: 2)
        let pausedFrames = try XCTUnwrap(field("frames"))
        switchApps()
        XCTAssertTrue(app.buttons["resume-game"].waitForExistence(timeout: 5))
        Thread.sleep(forTimeInterval: 2)
        XCTAssertEqual(field("generation"), generation)
        XCTAssertEqual(field("frames"), pausedFrames, "Background/foreground must not clear the user's HOME pause.")
        capture("background-retains-home-pause")
        app.buttons["resume-game"].tap()
        waitForFrames(after: pausedFrames)
        app.buttons["home-button"].tap()
        app.buttons["exit-game"].tap()
        XCTAssertTrue(app.buttons["import-content"].waitForExistence(timeout: 15))
        app.terminate()
    }

    func testGameControlsRotateAndExit() throws {
        let app = launch()
        XCTAssertTrue(app.staticTexts["JIT pronto"].waitForExistence(timeout: 20))
        // A real game when installed in this simulator (PCSE00890),
        // otherwise the homebrew probe app: both exercise the full session
        // surface (MoltenVK on device, the same input/overlay paths).
        let game: XCUIElement
        if app.buttons["game-PCSE00890"].exists {
            game = app.buttons["game-PCSE00890"]
        } else if app.buttons["game-XERP00001"].exists {
            game = app.buttons["game-XERP00001"]
        } else {
            throw XCTSkip("Install PCSE00890 (or XERP00001) in this simulator to exercise the real game surface.")
        }
        game.tap()
        // Fullscreen play: no top bar; HOME (top center) is the only
        // in-game control (pause + back to the library).
        let home = app.buttons["home-button"]
        XCTAssertTrue(home.waitForExistence(timeout: 30))
        let cross = app.buttons["pad-12"]
        XCTAssertTrue(cross.waitForExistence(timeout: 60))
        let surface = app.otherElements["game-surface"]
        // The surface starts at the very top of the screen (below the
        // island strip); HOME floats over it.
        XCTAssertLessThan(surface.frame.minY, 120)
        // Let the initial title scene replace the startup framebuffer before
        // capturing. Input remains enabled throughout the rotation sequence.
        Thread.sleep(forTimeInterval: 3)
        capture("game-portrait")
        for orientation in [UIDeviceOrientation.landscapeLeft, .landscapeRight] {
            rotate(orientation, app: app)
            XCTAssertTrue(home.isHittable)
            XCTAssertTrue(cross.isHittable)
            XCTAssertTrue(app.windows.firstMatch.frame.contains(cross.frame))
            capture("game-\(orientation.rawValue)")
        }
        // HOME pauses the game and opens the pause menu.
        home.tap()
        let resume = app.buttons["resume-game"]
        XCTAssertTrue(resume.waitForExistence(timeout: 5))
        XCTAssertEqual(resume.label, "Continuar")
        XCTAssertTrue(app.buttons["exit-game"].exists)
        XCTAssertEqual(app.buttons["exit-game"].label, "Voltar à biblioteca")
        capture("game-paused-menu")
        resume.tap()
        XCTAssertTrue(cross.waitForExistence(timeout: 10))
        // HOME again -> menu -> back to the library.
        home.tap()
        let exit = app.buttons["exit-game"]
        XCTAssertTrue(exit.waitForExistence(timeout: 5))
        exit.tap()
        XCTAssertTrue(app.buttons["import-content"].waitForExistence(timeout: 10))
        app.terminate()
    }
}
