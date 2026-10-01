import Foundation

enum AppLanguage: String, CaseIterable, Identifiable {
    case english = "en"
    case brazilianPortuguese = "pt-BR"

    static let preference = "interface.language"
    static func saved(in defaults: UserDefaults = .standard) -> Self {
        Self(rawValue: defaults.string(forKey: preference) ?? "") ?? .english
    }

    var id: String { rawValue }
    var locale: Locale { Locale(identifier: rawValue) }
    // Autonyms keep the selector recognizable in either interface language.
    var name: String {
        switch self {
        case .english: "English"
        case .brazilianPortuguese: "Português (Brasil)"
        }
    }

    var bundle: Bundle {
        guard let path = Bundle.main.path(forResource: rawValue, ofType: "lproj"),
              let bundle = Bundle(path: path) else { return .main }
        return bundle
    }
}

/// SwiftUI resolves literal keys with the root view's locale. UIKit and
/// messages produced outside views use the same explicitly selected bundle.
/// Existing source phrases are stable keys, shared by both translation tables.
enum L10n {
    static func text(_ key: String, language: AppLanguage = .saved()) -> String {
        let fallback = AppLanguage.english.bundle.localizedString(forKey: key, value: key, table: nil)
        return language.bundle.localizedString(forKey: key, value: fallback, table: nil)
    }

    static func format(_ key: String, _ arguments: CVarArg..., language: AppLanguage = .saved()) -> String {
        String(format: text(key, language: language), locale: language.locale, arguments: arguments)
    }

    static func installedGames(_ count: Int, language: AppLanguage = .saved()) -> String {
        count == 0 ? text("Nenhum jogo instalado", language: language)
            : format("library.installedGames", count, language: language)
    }
}
