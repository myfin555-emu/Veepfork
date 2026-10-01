import XCTest
import UIKit
@testable import Vita3K

final class LocalizationTests: XCTestCase {
    func testEnglishDefaultAndSavedLanguageFallback() throws {
        let name = "localization-tests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: name))
        defer { defaults.removePersistentDomain(forName: name) }
        defaults.set(["pt-BR"], forKey: "AppleLanguages")
        XCTAssertEqual(AppLanguage.saved(in: defaults), .english)
        defaults.set("pt-BR", forKey: AppLanguage.preference)
        XCTAssertEqual(AppLanguage.saved(in: defaults), .brazilianPortuguese)
        defaults.set("unsupported", forKey: AppLanguage.preference)
        XCTAssertEqual(AppLanguage.saved(in: defaults), .english)
    }

    func testBothTranslationTablesAreBundledAndComplete() throws {
        func table(_ language: AppLanguage, extension suffix: String) throws -> [String: Any] {
            let url = try XCTUnwrap(language.bundle.url(forResource: "Localizable", withExtension: suffix))
            return try XCTUnwrap(PropertyListSerialization.propertyList(from: Data(contentsOf: url), format: nil) as? [String: Any])
        }
        let english = try table(.english, extension: "strings")
        let portuguese = try table(.brazilianPortuguese, extension: "strings")
        XCTAssertEqual(Set(english.keys), Set(portuguese.keys))
        XCTAssertTrue(english.values.allSatisfy { ($0 as? String)?.isEmpty == false })
        XCTAssertTrue(portuguese.values.allSatisfy { ($0 as? String)?.isEmpty == false })
        XCTAssertEqual(Set(try table(.english, extension: "stringsdict").keys),
                       Set(try table(.brazilianPortuguese, extension: "stringsdict").keys))
        XCTAssertEqual(L10n.text("Opções", language: .english), "Settings")
        XCTAssertEqual(L10n.text("Opções", language: .brazilianPortuguese), "Opções")
        XCTAssertEqual(L10n.text("Enable Shader Cache", language: .brazilianPortuguese), "Ativar cache de shaders")
    }

    func testCountsAndRuntimeMessagesInBothLanguages() {
        XCTAssertEqual(L10n.installedGames(0, language: .english), "No games installed")
        XCTAssertEqual(L10n.installedGames(1, language: .english), "1 game installed")
        XCTAssertEqual(L10n.installedGames(2, language: .english), "2 games installed")
        XCTAssertEqual(L10n.installedGames(0, language: .brazilianPortuguese), "Nenhum jogo instalado")
        XCTAssertEqual(L10n.installedGames(1, language: .brazilianPortuguese), "1 jogo instalado")
        XCTAssertEqual(L10n.installedGames(2, language: .brazilianPortuguese), "2 jogos instalados")
        XCTAssertEqual(L10n.format("Erro ao salvar %@ (código %d).", "shader_cache", Int32(-1), language: .english),
                       "Could not save shader_cache (code -1).")
        XCTAssertEqual(L10n.format("Erro ao salvar %@ (código %d).", "shader_cache", Int32(-1), language: .brazilianPortuguese),
                       "Erro ao salvar shader_cache (código -1).")
    }

    @MainActor
    func testUIKitAndExistingSyncStatusFollowSavedLanguage() {
        let defaults = UserDefaults.standard
        let previous = defaults.object(forKey: AppLanguage.preference)
        defer { defaults.set(previous, forKey: AppLanguage.preference) }
        let sync = CloudSaveSync()
        for (language, cancel, unavailable) in [
            (AppLanguage.english, "Cancel", "iCloud is unavailable."),
            (.brazilianPortuguese, "Cancelar", "iCloud indisponível.")
        ] {
            defaults.set(language.rawValue, forKey: AppLanguage.preference)
            let keyboard = GameKeyboardView(frame: .zero)
            let row = keyboard.subviews.compactMap { $0 as? UIStackView }.first
            XCTAssertEqual((row?.arrangedSubviews.first as? UIButton)?.configuration?.title, cancel)
            XCTAssertTrue(SaveSyncError.unavailable.localizedDescription.hasPrefix(unavailable))
            XCTAssertEqual(sync.status, L10n.text(sync.enabled ? "Aguardando sincronização." : "Sincronização desativada.", language: language))
        }
    }
}
