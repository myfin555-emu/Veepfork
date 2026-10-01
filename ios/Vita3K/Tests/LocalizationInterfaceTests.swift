import XCTest

@MainActor
final class LocalizationInterfaceTests: XCTestCase {
    func testLanguageSwitchUpdatesInterfaceAndSurvivesRelaunch() {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchEnvironment["VITA3K_UI_TEST"] = "1"
        app.launchEnvironment["VITA3K_UI_TEST_NO_JIT"] = "1"
        app.launchArguments = ["-AppleLanguages", "(pt-BR)", "-AppleLocale", "pt_BR", "-interface.forceLandscape", "NO"]

        func dismissJIT() {
            let alert = app.alerts.firstMatch
            XCTAssertTrue(alert.waitForExistence(timeout: 20))
            let english = alert.buttons["Continue to library"]
            (english.exists ? english : alert.buttons["Continuar na biblioteca"]).tap()
        }
        func openSettings() {
            let english = app.buttons["Settings"]
            (english.exists ? english : app.buttons["Opções"]).tap()
            XCTAssertTrue(app.buttons["interface-language"].waitForExistence(timeout: 5))
        }
        func select(_ name: String) {
            app.buttons["interface-language"].tap()
            app.buttons[name].tap()
        }
        app.launch()
        dismissJIT()
        openSettings()
        // Normalize an existing installation; a fresh install is also covered
        // by the unit test with Portuguese device preferences and no app setting.
        if app.buttons["Opções"].exists { select("English") }
        XCTAssertTrue(app.staticTexts["Language"].exists)
        XCTAssertTrue(app.staticTexts["Appearance"].exists)
        select("Português (Brasil)")
        XCTAssertTrue(app.buttons["Opções"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["Idioma"].exists)
        XCTAssertTrue(app.staticTexts["Aparência"].exists)
        app.buttons["Biblioteca"].tap()
        XCTAssertTrue(app.staticTexts["Sua biblioteca"].exists)
        XCTAssertTrue(app.staticTexts.matching(NSPredicate(format: "label MATCHES %@",
            "Nenhum jogo instalado|[0-9]+ jogos? instalados?")).firstMatch.exists)
        app.terminate()

        app.launch()
        XCTAssertTrue(app.alerts["Abra pelo StikDebug"].waitForExistence(timeout: 20))
        dismissJIT()
        openSettings()
        select("English")
        XCTAssertTrue(app.buttons["Settings"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["Language"].exists)
        app.buttons["Library"].tap()
        XCTAssertTrue(app.staticTexts["Your library"].exists)
        XCTAssertTrue(app.staticTexts.matching(NSPredicate(format: "label MATCHES %@",
            "No games installed|[0-9]+ games? installed")).firstMatch.exists)
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = "english-library"
        attachment.lifetime = .keepAlways
        add(attachment)
        app.terminate()

        app.launch()
        XCTAssertTrue(app.alerts["Open with StikDebug"].waitForExistence(timeout: 20))
        dismissJIT()
        XCTAssertTrue(app.buttons["Library"].exists)
        app.terminate()
    }
}
