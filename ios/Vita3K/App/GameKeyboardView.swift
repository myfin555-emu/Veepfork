import UIKit

struct GameKeyboardRequest {
    let id: UInt64
    let text: String
    let title: String
    let caret: Int
    let maxLength: Int // Vita limits count UTF-16 code units, not Swift Characters.
    let keyboardType: UInt32
    let multiline: Bool
    let cancelable: Bool

    func limited(_ value: String) -> String {
        var result = ""
        var count = 0
        for character in value {
            let size = character.utf16.count
            guard count + size <= maxLength else { break }
            result.append(character)
            count += size
        }
        return result
    }
}

/// UIKit owns composition, selection and hardware-keyboard editing. Only
/// committed text crosses into the emulator; marked text remains editable.
@MainActor
final class GameKeyboardView: UIView, UITextViewDelegate {
    let textView = UITextView()
    private let done = UIButton(configuration: .borderedProminent())
    private let cancel = UIButton(configuration: .bordered())
    private(set) var request: GameKeyboardRequest?
    private var applyingSnapshot = false
    var onEdit: ((UInt64, String, UInt32) -> Bool)?
    var onFinish: ((UInt64, Bool) -> Bool)?

    override var intrinsicContentSize: CGSize {
        CGSize(width: UIView.noIntrinsicMetric, height: request?.multiline == true ? 88 : 56)
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .black.withAlphaComponent(0.9)
        textView.backgroundColor = .darkGray
        textView.textColor = .white
        textView.layer.cornerRadius = 6
        textView.font = .systemFont(ofSize: 16)
        textView.autocorrectionType = .no
        textView.autocapitalizationType = .none
        textView.smartQuotesType = .no
        textView.smartDashesType = .no
        textView.delegate = self
        textView.accessibilityIdentifier = "game-keyboard-text"
        textView.setContentHuggingPriority(.defaultLow, for: .horizontal)
        done.configuration?.title = L10n.text("OK")
        done.accessibilityIdentifier = "game-keyboard-confirm"
        cancel.configuration?.title = L10n.text("Cancelar")
        cancel.configuration?.baseForegroundColor = .white
        cancel.accessibilityIdentifier = "game-keyboard-cancel"
        done.addAction(UIAction { [weak self] _ in self?.finish(cancelled: false) }, for: .touchUpInside)
        cancel.addAction(UIAction { [weak self] _ in self?.finish(cancelled: true) }, for: .touchUpInside)
        let row = UIStackView(arrangedSubviews: [cancel, textView, done])
        row.spacing = 8
        row.alignment = .fill
        row.translatesAutoresizingMaskIntoConstraints = false
        addSubview(row)
        NSLayoutConstraint.activate([
            row.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            row.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),
            row.topAnchor.constraint(equalTo: topAnchor, constant: 8),
            row.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -8),
            done.widthAnchor.constraint(greaterThanOrEqualToConstant: 44)
        ])
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func present(_ request: GameKeyboardRequest) {
        applyingSnapshot = true
        self.request = request
        textView.text = request.limited(request.text)
        textView.accessibilityLabel = request.title.isEmpty ? L10n.text("Texto do jogo") : request.title
        textView.selectedRange = NSRange(location: min(request.caret, textView.text.utf16.count), length: 0)
        switch request.keyboardType {
        case 1: textView.keyboardType = .asciiCapable
        case 2: textView.keyboardType = .numberPad
        case 3: textView.keyboardType = .numbersAndPunctuation
        case 4: textView.keyboardType = .URL
        case 5: textView.keyboardType = .emailAddress
        default: textView.keyboardType = .default
        }
        textView.returnKeyType = request.multiline ? .default : .done
        cancel.isHidden = !request.cancelable
        isHidden = false
        invalidateIntrinsicContentSize()
        applyingSnapshot = false
        textView.becomeFirstResponder()
        textView.reloadInputViews()
    }

    func dismiss() {
        request = nil
        textView.resignFirstResponder()
        textView.text = ""
        isHidden = true
    }

    @discardableResult
    private func commit() -> Bool {
        guard !applyingSnapshot, let request, textView.markedTextRange == nil else { return false }
        let current = textView.text ?? ""
        let limited = request.limited(current)
        if limited != current {
            let caret = min(textView.selectedRange.location, limited.utf16.count)
            applyingSnapshot = true
            textView.text = limited
            textView.selectedRange = NSRange(location: caret, length: 0)
            applyingSnapshot = false
        }
        let caret = UInt32(min(textView.selectedRange.location, limited.utf16.count))
        return onEdit?(request.id, limited, caret) ?? false
    }

    func finish(cancelled: Bool) {
        guard let request, !cancelled || request.cancelable else { return }
        if !cancelled {
            textView.unmarkText()
            guard commit() else { return }
        }
        if onFinish?(request.id, cancelled) == true { dismiss() }
    }

    func textViewDidChange(_ textView: UITextView) { commit() }
    func textViewDidChangeSelection(_ textView: UITextView) { commit() }

    func textView(_ textView: UITextView, shouldChangeTextIn range: NSRange, replacementText text: String) -> Bool {
        if text == "\n", request?.multiline == false, textView.markedTextRange == nil {
            finish(cancelled: false)
            return false
        }
        return true
    }
}
