// Vita3K iOS shell app

import SwiftUI
import Foundation
import UIKit

@MainActor
enum AppOrientation {
    static let preference = "interface.forceLandscape"

    static var supportedOrientations: UIInterfaceOrientationMask {
        if UserDefaults.standard.bool(forKey: preference) { return .landscape }
        return UIDevice.current.userInterfaceIdiom == .pad ? .all : .allButUpsideDown
    }

    static func apply() {
        for case let scene as UIWindowScene in UIApplication.shared.connectedScenes
            where scene.activationState == .foregroundActive {
            for window in scene.windows {
                invalidateOrientations(in: window.rootViewController)
            }
            // Rotate the actual window so layout, safe areas, Metal drawable
            // dimensions and touch coordinates all use the same orientation.
            scene.requestGeometryUpdate(.iOS(interfaceOrientations: supportedOrientations)) { error in
                Probe.mark("orientation-request-failed: \(error.localizedDescription)")
            }
        }
    }

    private static func invalidateOrientations(in controller: UIViewController?) {
        guard let controller else { return }
        controller.setNeedsUpdateOfSupportedInterfaceOrientations()
        for child in controller.children {
            invalidateOrientations(in: child)
        }
        invalidateOrientations(in: controller.presentedViewController)
    }
}

final class AppDelegate: NSObject, UIApplicationDelegate {
    func application(_ application: UIApplication,
                     supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        // Apply the saved preference before SwiftUI creates the library or
        // presents a game, and keep it in force across controller transitions.
        AppOrientation.supportedOrientations
    }
}

@main
struct Vita3KApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @Environment(\.scenePhase) private var scenePhase
    @AppStorage(AppOrientation.preference) private var forceLandscape = false
    @AppStorage(AppLanguage.preference) private var language = AppLanguage.english.rawValue
    @StateObject private var controller = CoreController()

    init() {
        // The core's Vulkan renderer creates the VK_EXT_metal_surface on the
        // launch (background) thread; MoltenVK sets the CAMetalLayer's
        // colorspace there. On a Debug simulator, CoreAnimation aborts on any
        // off-main-thread CALayer mutation (CA_ASSERT_MAIN_THREAD_TRANSACTIONS).
        // Device (Release) treats it as a warning, which is why the step-6
        // demo runs fine there. Match device behavior on the simulator.
        setenv("CA_ASSERT_MAIN_THREAD_TRANSACTIONS", "0", 1)
        Probe.mark("swift-app-init")
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(controller)
                .environment(\.locale, (AppLanguage(rawValue: language) ?? .english).locale)
                .onAppear { AppOrientation.apply() }
                .onChange(of: forceLandscape) { _ in AppOrientation.apply() }
                .onChange(of: scenePhase) { phase in
                    if phase == .active { AppOrientation.apply() }
                }
        }
    }
}
