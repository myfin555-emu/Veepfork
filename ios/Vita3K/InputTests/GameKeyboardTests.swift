import UIKit
import XCTest
@testable import Vita3K

@MainActor
final class GameKeyboardTests: XCTestCase {
    func testInactiveSessionRejectsKeyboardActions() {
        XCTAssertNil(Vita3KCore.imeSnapshot())
        XCTAssertFalse(Vita3KCore.imeReplace(id: 1, text: "Nome", caret: 4))
        XCTAssertFalse(Vita3KCore.imeFinish(id: 1, cancel: false))
    }

    private func request(id: UInt64 = 1, text: String = "Sinner", limit: Int = 12,
                         multiline: Bool = false, cancelable: Bool = true) -> GameKeyboardRequest {
        GameKeyboardRequest(id: id, text: text, title: "Nome do personagem", caret: text.utf16.count,
                            maxLength: limit, keyboardType: 0, multiline: multiline, cancelable: cancelable)
    }

    func testInitialTextSelectionEditsAndConfirmReachGuest() {
        let keyboard = GameKeyboardView()
        var edited = ""
        var caret: UInt32 = 0
        var finished: UInt64?
        keyboard.onEdit = { id, text, position in
            XCTAssertEqual(id, 1)
            edited = text
            caret = position
            return true
        }
        keyboard.onFinish = { id, cancel in XCTAssertFalse(cancel); finished = id; return true }
        keyboard.present(request())
        XCTAssertEqual(keyboard.textView.text, "Sinner")
        XCTAssertEqual(keyboard.textView.selectedRange.location, 6)
        // UIKit supplies the complete edited text, including middle edits.
        keyboard.textView.text = "José🙂"
        keyboard.textView.selectedRange = NSRange(location: 2, length: 0)
        keyboard.textViewDidChange(keyboard.textView)
        XCTAssertEqual(edited, "José🙂")
        XCTAssertEqual(caret, 2)
        XCTAssertFalse(keyboard.textView(keyboard.textView, shouldChangeTextIn: NSRange(location: 2, length: 0), replacementText: "\n"))
        XCTAssertEqual(finished, 1)
        XCTAssertTrue(keyboard.isHidden)
        XCTAssertNil(keyboard.request)
    }

    func testUTF16LimitKeepsWholeCharactersAndAllowsDeletingEverything() {
        let keyboard = GameKeyboardView()
        var edited = ""
        keyboard.onEdit = { _, text, _ in edited = text; return true }
        keyboard.present(request(text: "", limit: 5))
        keyboard.textView.text = "José🙂x"
        keyboard.textViewDidChange(keyboard.textView)
        XCTAssertEqual(edited, "José") // emoji needs two UTF-16 units
        XCTAssertEqual(keyboard.textView.text, "José")
        keyboard.textView.text = ""
        keyboard.textViewDidChange(keyboard.textView)
        XCTAssertEqual(edited, "")
    }

    func testCancellationPermissionsAndConsecutiveRequests() {
        let keyboard = GameKeyboardView()
        var finished: UInt64?
        keyboard.onFinish = { id, cancel in XCTAssertTrue(cancel); finished = id; return true }
        keyboard.present(request(cancelable: false))
        keyboard.finish(cancelled: true)
        XCTAssertNil(finished)
        XCTAssertFalse(keyboard.isHidden)
        keyboard.present(request(id: 2, text: "Novo nome"))
        XCTAssertEqual(keyboard.textView.text, "Novo nome")
        keyboard.finish(cancelled: true)
        XCTAssertEqual(finished, 2)
        XCTAssertTrue(keyboard.isHidden)
    }

    func testMultilineReturnDoesNotSubmitAndFailedFinishStaysOpen() {
        let keyboard = GameKeyboardView()
        keyboard.onEdit = { _, _, _ in true }
        keyboard.onFinish = { _, _ in false }
        keyboard.present(request(multiline: true))
        XCTAssertTrue(keyboard.textView(keyboard.textView, shouldChangeTextIn: NSRange(location: 0, length: 0), replacementText: "\n"))
        keyboard.finish(cancelled: false)
        XCTAssertFalse(keyboard.isHidden)
    }

    func testCompositionIsNotCommittedUntilUIKitFinishesIt() {
        let keyboard = GameKeyboardView()
        var edited = ""
        keyboard.onEdit = { _, text, _ in edited = text; return true }
        keyboard.present(request(text: ""))
        keyboard.textView.setMarkedText("に", selectedRange: NSRange(location: 1, length: 0))
        XCTAssertNotNil(keyboard.textView.markedTextRange)
        keyboard.textViewDidChange(keyboard.textView)
        XCTAssertEqual(edited, "")
        XCTAssertTrue(keyboard.textView(keyboard.textView, shouldChangeTextIn: NSRange(location: 1, length: 0), replacementText: "\n"))
        keyboard.textView.unmarkText()
        keyboard.textViewDidChange(keyboard.textView)
        XCTAssertEqual(edited, "に")
    }

    func testPresentFocusesTheNativeKeyboardAndConfirmDismissesIt() throws {
        let scene = try XCTUnwrap(UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first)
        let window = UIWindow(windowScene: scene)
        window.rootViewController = UIViewController()
        window.makeKeyAndVisible()
        defer { window.isHidden = true }
        let root = try XCTUnwrap(window.rootViewController?.view)
        let keyboard = GameKeyboardView()
        keyboard.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(keyboard)
        NSLayoutConstraint.activate([
            keyboard.leadingAnchor.constraint(equalTo: root.safeAreaLayoutGuide.leadingAnchor),
            keyboard.trailingAnchor.constraint(equalTo: root.safeAreaLayoutGuide.trailingAnchor),
            keyboard.bottomAnchor.constraint(equalTo: root.keyboardLayoutGuide.topAnchor)
        ])
        keyboard.onEdit = { _, _, _ in true }
        keyboard.onFinish = { _, _ in true }
        keyboard.present(request())
        root.layoutIfNeeded()
        XCTAssertTrue(keyboard.textView.isFirstResponder)
        XCTAssertGreaterThan(keyboard.textView.frame.width, 0)
        XCTAssertGreaterThan(keyboard.frame.height, 40)
        XCTAssertFalse(keyboard.hasAmbiguousLayout)
        keyboard.finish(cancelled: false)
        XCTAssertFalse(keyboard.textView.isFirstResponder)
        XCTAssertTrue(keyboard.isHidden)
    }
}
