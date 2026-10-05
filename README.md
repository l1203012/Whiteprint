# Whiteprint-Notetaking-Application
An notetaking application that I use for my day to day notetaking. Made for MacOS and Windows 11.

## Development

The app is being built milestone by milestone; see [docs/BETA_PLAN.md](docs/BETA_PLAN.md).

- `WhiteprintCore` (Swift package, macOS 13+): the `.wprint` note format and the drawing language ([docs/DSL.md](docs/DSL.md)).
- Build and test with `swift build` and `swift test`. These need Xcode (15.2 recommended), not just the Command Line Tools.
