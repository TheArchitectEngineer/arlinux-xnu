// SPDX-License-Identifier: GPL-3.0-or-later
// Small runner entry around PlayCover's unchanged, pinned Macho implementation.
import Foundation

@main
enum PlayCoverReferenceConverter {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else {
            fatalError("Expected the copied app's executable path")
        }
        let executable = URL(fileURLWithPath: CommandLine.arguments[1])
        guard try !Macho.isMachoEncrypted(atURL: executable) else {
            throw PlayCoverError.appEncrypted
        }
        try Macho.convertMacho(executable)
    }
}
