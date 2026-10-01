import XCTest
import UIKit
@testable import Vita3K

@MainActor
final class MetalSurfaceTests: XCTestCase {
    func testDrawableUsesScreenPixelsAcrossLayouts() throws {
        let scene = try XCTUnwrap(UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first)
        let window = UIWindow(windowScene: scene)
        window.rootViewController = UIViewController()
        window.isHidden = false
        defer { window.isHidden = true }

        let surface = MetalSurfaceView()
        window.rootViewController!.view.addSubview(surface)
        let scale = scene.screen.nativeScale
        XCTAssertGreaterThan(scale, 1, "Run on a Retina simulator to catch a points-sized drawable.")

        for size in [CGSize(width: 390, height: 221), CGSize(width: 780, height: 340)] {
            // Reproduce the default CAMetalLayer-backed UIView scale, then
            // exercise the same layout path used when rotating the app.
            surface.contentScaleFactor = 1
            surface.frame = CGRect(origin: CGPoint(x: 12, y: 30), size: size)
            surface.setNeedsLayout()
            surface.layoutIfNeeded()

            let pixels = CGSize(width: (size.width * scale).rounded(), height: (size.height * scale).rounded())
            XCTAssertEqual(surface.contentScaleFactor, scale)
            XCTAssertEqual(surface.metalLayer.contentsScale, scale)
            XCTAssertEqual(surface.metalLayer.drawableSize, pixels)
            XCTAssertEqual(surface.drawableWidth, Int(pixels.width))
            XCTAssertEqual(surface.drawableHeight, Int(pixels.height))
            XCTAssertEqual(surface.frame.origin, CGPoint(x: 12, y: 30))
        }
    }
}
