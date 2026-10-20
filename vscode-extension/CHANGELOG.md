# Changelog

## 0.9.0 (unreleased)

- Updated for the brace-based Genesis+ 0.9 syntax.
- When the compiler fails without producing diagnostics (crash, wrong binary), the failure is reported in the *Genesis+* output channel instead of showing a clean file.
- Tests select the compiler from `GENESIS_BIN`, or the native binary only, and verify it runs.
- Error checking now runs the real compiler (`genesis check --stdin --machine`) instead of a separate approximation, so the editor and `genesis build` always agree.
- Completion is context-aware: it offers only the properties each element supports, padding sides, and align values.
- Added hover help, snippets, document formatting (`genesis fmt`), and commands to build the site and start or stop the dev server.
- Added `link`, `button` and `image` elements and the `level`, `align`, `width`, `title`, `to` and `alt` properties.
- The `.gns` file icon is now also registered as a language icon.
- Workspace trust: in untrusted folders the extension never runs a compiler found inside the folder.

## 0.0.1

- Initial version: syntax highlighting, line-based diagnostics, basic completion, and a file icon theme.
