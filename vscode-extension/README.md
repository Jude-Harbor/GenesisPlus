# Genesis+ for Visual Studio Code

Language support for [Genesis+](../README.md), a small language for building websites.

## Features

- **Syntax highlighting** for `.gns` files: elements, properties, strings, colors, numbers, comments, and braces. The old `:` block syntax is highlighted as an error.
- **Live error checking.** As you type, the file is checked by the real Genesis+ compiler, so the editor shows exactly the errors `genesis build` would report.
- **Completion** that knows which properties each element supports, plus element and padding-side suggestions.
- **Hover help** for every element and property.
- **Snippets**: `page`, `column`, `row`, `card`, `heading`, `text`, `link`, `button`, `image`.
- **Formatting** (Format Document, or format on save) using `genesis fmt`.
- **Brace matching**, auto-closing braces and quotes, and `#` comment toggling.
- **Commands**: *Genesis+: Build Website*, *Genesis+: Start Dev Server*, *Genesis+: Stop Dev Server*.
- **File icon** for `.gns` files. It is used automatically by icon themes that show language icons. An optional *Genesis+ Icons* theme is included, but it only defines the `.gns` icon, so other files show no icon while it is selected.

## Requirements

Error checking, formatting, and the build and dev-server commands need the Genesis+ compiler. Build it as described in the main README. The extension looks for it in this order:

1. The `genesis.compilerPath` setting.
2. `genesis.exe` (Windows) or `genesis` in the workspace folder. This is only used in trusted workspaces.
3. `genesis` on your `PATH`.

If the compiler cannot be found, you will see a warning once. Highlighting, completion, hovers, and snippets still work.

## Settings

| Setting | Default | Description |
| --- | --- | --- |
| `genesis.compilerPath` | `""` | Full path to the compiler. |
| `genesis.diagnostics.enable` | `true` | Check files for errors while typing. |
| `genesis.devServer.port` | `3000` | Port for *Start Dev Server*. |

`.gns` files default to 4-space indentation. Indentation is only for readability; Genesis+ uses braces for structure.

## Development

1. Open the repository root in VS Code.
2. Build the compiler (`make`) so the extension can find `genesis.exe` / `genesis` in the workspace.
3. Press **F5** and choose **Launch Genesis+ Extension**. A new VS Code window opens with the extension loaded.
4. Open any `.gns` file, for example `examples/landing/lead.gns`.

Run the automated tests (Node.js 18 or newer, no dependencies). From the repository root, `make test-extension` builds the compiler and runs them against it. From this folder:

```bash
npm test
```

Compiler selection in the tests: if `GENESIS_BIN` is set, that binary is used, and the tests fail if it does not exist or does not run. Otherwise only the native binary in the repository root is used (`genesis.exe` on Windows, `genesis` elsewhere) after checking that it runs; if there is none, the compiler-backed tests are skipped with a message.

## Packaging

The extension has no runtime dependencies. To produce a `.vsix` file:

```bash
npx @vscode/vsce package
```

Install it with **Extensions: Install from VSIX...**. Packaging has not yet been verified for 0.9.0. Before publishing to the Marketplace, add a `repository` field to `package.json` (the repository URL is not decided yet) and complete the copyright holder in `LICENSE`; until then the extension is not Marketplace-ready.

## License

MIT; see [LICENSE](LICENSE).
