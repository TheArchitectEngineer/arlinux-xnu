// Runner-side GUI input/normal-termination driver for the real reference app.
// Uses macOS window and event APIs. A posted event is never itself a pass;
// the app callback log and rendered label establish whether it was delivered.
import AppKit
import ApplicationServices
import Foundation

@main
enum ReferenceUI {
    static func main() throws {
        let arguments = CommandLine.arguments
        guard arguments.count == 2 else { fatalError("Expected click or quit") }
        let applications = NSRunningApplication.runningApplications(withBundleIdentifier: "io.arlinux.ARHello")
        guard let app = applications.first else { fatalError("ARHello is not running") }
        print("REFERENCE_GUI pid=\(app.processIdentifier) trusted=\(AXIsProcessTrusted())")
        if arguments[1] == "quit" {
            guard app.terminate() else { fatalError("Normal termination request rejected") }
            return
        }
        guard arguments[1] == "click" else { fatalError("Unknown action") }
        app.activate(options: [.activateIgnoringOtherApps])
        // The public window list establishes the real visible app window.
        // CoreGraphics and the screen snapshot both use top-left coordinates.
        let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] ?? []
        guard let window = windows.first(where: {
            ($0[kCGWindowOwnerPID as String] as? Int32) == app.processIdentifier &&
            ($0[kCGWindowLayer as String] as? Int) == 0
        }), let dictionary = window[kCGWindowBounds as String] as? CFDictionary,
        let bounds = CGRect(dictionaryRepresentation: dictionary) else {
            fatalError("No visible ARHello window")
        }
        // Catalyst's normal iPad-sized 1024-point window is scaled to the host.
        // ARHello's button center is (160,210) in its authored UIKit layout.
        let scale = bounds.width / 1024.0
        let point = CGPoint(x: bounds.minX + 160 * scale, y: bounds.minY + 210 * scale)
        print("REFERENCE_GUI window=\(bounds) click=\(point)")
        guard let down = CGEvent(mouseEventSource: nil, mouseType: .leftMouseDown,
                                 mouseCursorPosition: point, mouseButton: .left),
              let up = CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp,
                               mouseCursorPosition: point, mouseButton: .left) else {
            fatalError("Cannot create host mouse events")
        }
        down.postToPid(app.processIdentifier)
        Thread.sleep(forTimeInterval: 0.08)
        up.postToPid(app.processIdentifier)
    }
}
