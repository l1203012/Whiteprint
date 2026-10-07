// Ed25519 keys and signatures for app updates (see docs/RELEASING.md).
//
//   swift Scripts/update-signing.swift generate KEYFILE   new private key into KEYFILE (mode 600);
//                                                         prints the public key for Info.plist
//   swift Scripts/update-signing.swift public             public key of $UPDATE_SIGNING_KEY
//   swift Scripts/update-signing.swift sign FILE          base64 signature of FILE with $UPDATE_SIGNING_KEY
//
// Keys are the raw 32 bytes, base64-encoded. The app checks a DMG against the
// public key in WhiteprintUpdatePublicKey before installing it.
import CryptoKit
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data((message + "\n").utf8))
    exit(1)
}

func privateKey() -> Curve25519.Signing.PrivateKey {
    guard let text = ProcessInfo.processInfo.environment["UPDATE_SIGNING_KEY"], !text.isEmpty else {
        fail("UPDATE_SIGNING_KEY is not set")
    }
    guard let data = Data(base64Encoded: text.trimmingCharacters(in: .whitespacesAndNewlines)),
          let key = try? Curve25519.Signing.PrivateKey(rawRepresentation: data) else {
        fail("UPDATE_SIGNING_KEY is not a base64 Ed25519 private key")
    }
    return key
}

let arguments = CommandLine.arguments.dropFirst()
switch (arguments.first, arguments.count) {
case ("generate", 2):
    let path = arguments.last!
    guard !FileManager.default.fileExists(atPath: path) else { fail("\(path) already exists; not replacing a key") }
    let key = Curve25519.Signing.PrivateKey()
    guard FileManager.default.createFile(atPath: path, contents: Data(key.rawRepresentation.base64EncodedString().utf8),
                                         attributes: [.posixPermissions: 0o600]) else { fail("couldn't write \(path)") }
    print(key.publicKey.rawRepresentation.base64EncodedString())
case ("public", 1):
    print(privateKey().publicKey.rawRepresentation.base64EncodedString())
case ("sign", 2):
    guard let data = FileManager.default.contents(atPath: arguments.last!) else { fail("can't read \(arguments.last!)") }
    print(try privateKey().signature(for: data).base64EncodedString())
default:
    fail("usage: update-signing.swift generate KEYFILE | public | sign FILE")
}
