import Darwin
import Metal
import UIKit

/// Average guest frame interval over the sampling window, not input latency
/// or GPU execution time. A stopped frame counter must produce 0 FPS.
struct GuestFrameSampler {
    struct Reading {
        let fps: Double
        let frameMilliseconds: Double?
    }

    private var previous: (frames: UInt64, time: TimeInterval)?

    mutating func reset() { previous = nil }

    mutating func sample(frames: UInt64, time: TimeInterval, paused: Bool) -> Reading? {
        guard !paused else {
            reset()
            return nil
        }
        defer { previous = (frames, time) }
        guard let previous, time > previous.time, frames >= previous.frames else { return nil }
        let elapsed = time - previous.time
        let count = Double(frames - previous.frames)
        return Reading(fps: count / elapsed, frameMilliseconds: count > 0 ? elapsed * 1000 / count : nil)
    }
}

struct PerformanceMemory {
    let footprintBytes: UInt64?
    let gpuAllocatedBytes: UInt64?

    static func read(device: (any MTLDevice)?) -> Self {
        var info = task_vm_info_data_t()
        let capacity = MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<integer_t>.size
        var count = mach_msg_type_number_t(capacity)
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: capacity) {
                task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
            }
        }
        // Apple silicon uses unified memory: footprint already accounts for
        // charged GPU pages. Metal's allocated size is a separate allocation
        // metric, not extra physical VRAM to add to this footprint.
        #if targetEnvironment(simulator)
        // Simulator Metal reports zero even with live buffers; unavailable,
        // rather than a measured zero-byte GPU footprint.
        let gpuBytes: UInt64? = nil
        #else
        let gpuBytes = device.map { UInt64($0.currentAllocatedSize) }
        #endif
        return Self(footprintBytes: result == KERN_SUCCESS ? info.phys_footprint : nil,
                    gpuAllocatedBytes: gpuBytes)
    }
}

/// Compact, noninteractive card above the game surface and virtual controls.
@MainActor
final class PerformanceOverlayView: UIView {
    private let label = UILabel()

    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false
        backgroundColor = .black.withAlphaComponent(0.72)
        layer.cornerRadius = 8
        label.font = .monospacedSystemFont(ofSize: 11, weight: .medium)
        label.textColor = .white
        label.numberOfLines = 0
        label.accessibilityIdentifier = "performance-overlay"
        label.translatesAutoresizingMaskIntoConstraints = false
        addSubview(label)
        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            label.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -8),
            label.topAnchor.constraint(equalTo: topAnchor, constant: 6),
            label.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -6)
        ])
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func update(frames: GuestFrameSampler.Reading?, memory: PerformanceMemory, paused: Bool) {
        let fps = frames.map { String(format: "%.1f", $0.fps) } ?? "—"
        let milliseconds = frames?.frameMilliseconds.map { String(format: "%.1f ms", $0) } ?? "—"
        func mib(_ bytes: UInt64?) -> String {
            bytes.map { String(format: "%.0f MiB", Double($0) / 1_048_576) } ?? "—"
        }
        label.text = "\(paused ? L10n.text("Pausado") : "FPS  \(fps)")\n\(L10n.text("Quadro"))  \(milliseconds)\nRAM  \(mib(memory.footprintBytes))\n\(L10n.text("GPU aloc."))  \(mib(memory.gpuAllocatedBytes))"
    }
}
