import XCTest
import UIKit
@testable import Vita3K

/// UIKit controls -> C bridge -> the real CtrlState consumed by sceCtrl.
/// These tests need no retail game, controller or JIT execution.
@MainActor
final class VirtualGamepadTests: XCTestCase {
    private func makePad(size: CGSize = CGSize(width: 390, height: 460)) throws -> VirtualGamepadView {
        let documents = try XCTUnwrap(FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first)
        XCTAssertEqual(Vita3KCore.sessionInit(basePath: documents.path, staticAssets: Bundle.main.bundlePath), 0)
        let pad = VirtualGamepadView(frame: CGRect(origin: .zero, size: size))
        pad.setActive(true)
        pad.resetInput()
        pad.layoutIfNeeded()
        addTeardownBlock { @MainActor in pad.setActive(false) }
        return pad
    }

    private func button(_ tag: Int, in pad: VirtualGamepadView) throws -> UIButton {
        try XCTUnwrap(pad.subviews.compactMap { $0 as? UIButton }.first { $0.tag == tag })
    }

    private func assertCore(buttons: UInt32, extended: UInt32, active: Bool = true,
                            file: StaticString = #filePath, line: UInt = #line) {
        let report = Vita3KCore.sessionReport()
        XCTAssertTrue(report.contains("vpad=\(active ? "on" : "off") "), report, file: file, line: line)
        XCTAssertTrue(report.contains(String(format: "vpad_buttons=0x%08x ", buttons)), report, file: file, line: line)
        XCTAssertTrue(report.contains(String(format: "vpad_buttons_ext=0x%08x ", extended)), report, file: file, line: line)
    }

    func testEveryButtonReachesBothCtrlFormats() throws {
        let pad = try makePad()
        let expected: [(Int, UInt32, UInt32)] = [
            (0, 0x10, 0x10), (1, 0x80, 0x80), (2, 0x20, 0x20), (3, 0x40, 0x40),
            (10, 0x1000, 0x1000), (11, 0x8000, 0x8000),
            (12, 0x4000, 0x4000), (13, 0x2000, 0x2000),
            (20, 0x100, 0x400), (21, 0x200, 0x800), (30, 0x1, 0x1), (31, 0x8, 0x8)
        ]
        for (tag, normal, extended) in expected {
            let control = try button(tag, in: pad)
            XCTAssertTrue(pad.hitTest(CGPoint(x: control.frame.midX, y: control.frame.midY), with: nil) === control)
            control.sendActions(for: .touchDown)
            assertCore(buttons: normal, extended: extended)
            control.sendActions(for: .touchUpInside)
            assertCore(buttons: 0, extended: 0)
        }
    }

    func testChordReleaseAndCancellation() throws {
        let pad = try makePad()
        let up = try button(0, in: pad)
        let cross = try button(12, in: pad)
        up.sendActions(for: .touchDown)
        cross.sendActions(for: .touchDown)
        assertCore(buttons: 0x4010, extended: 0x4010)
        cross.sendActions(for: .touchUpOutside)
        assertCore(buttons: 0x10, extended: 0x10)
        up.sendActions(for: .touchCancel)
        assertCore(buttons: 0, extended: 0)
    }

    func testRotationClearsHeldInputWithoutDisablingVisiblePad() throws {
        let pad = try makePad()
        let cross = try button(12, in: pad)
        cross.sendActions(for: .touchDown)
        let sticks = pad.subviews.compactMap { $0 as? VirtualStickView }
        XCTAssertEqual(sticks.count, 2)
        sticks[0].onChange?(1, -1)
        sticks[1].onChange?(-0.5, 0.5)
        XCTAssertTrue(Vita3KCore.sessionReport().contains("vpad_axes=1.00,-1.00,-0.50,0.50"))

        // Same clear/reset pair used by viewWillTransition on rotation.
        Vita3KCore.inputClear()
        pad.resetInput()
        pad.frame.size = CGSize(width: 780, height: 340)
        pad.layoutIfNeeded()
        assertCore(buttons: 0, extended: 0)
        XCTAssertTrue(Vita3KCore.sessionReport().contains("vpad_axes=0.00,0.00,0.00,0.00"))
        XCTAssertTrue(pad.hitTest(CGPoint(x: cross.frame.midX, y: cross.frame.midY), with: nil) === cross)
        cross.sendActions(for: .touchDown)
        assertCore(buttons: 0x4000, extended: 0x4000)
        cross.sendActions(for: .touchUpInside)
        sticks[0].onChange?(0.75, 0)
        XCTAssertTrue(Vita3KCore.sessionReport().contains("vpad_axes=0.75,0.00,0.00,0.00"))
    }

    func testHideAndReenableDropsOldInput() throws {
        let pad = try makePad()
        let cross = try button(12, in: pad)
        cross.sendActions(for: .touchDown)
        pad.setActive(false) // physical controller connected or session stopped
        Vita3KCore.inputClear()
        assertCore(buttons: 0, extended: 0, active: false)
        XCTAssertNil(pad.hitTest(CGPoint(x: cross.frame.midX, y: cross.frame.midY), with: nil))
        pad.setActive(true)
        pad.layoutIfNeeded()
        assertCore(buttons: 0, extended: 0)
        cross.sendActions(for: .touchDown)
        assertCore(buttons: 0x4000, extended: 0x4000)
    }
}
