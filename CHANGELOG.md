# Changelog

All notable changes to Genesis+ are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](docs/versioning.md).

## [0.9.0] - unreleased (public testing release)

The first public testing release. **This is a breaking change from earlier development builds:** blocks now use braces instead of `:` and indentation (see *Migrating* in the README). The language may still change before 1.0; see [docs/versioning.md](docs/versioning.md).

### Changed

- **Brace-based blocks.** `page`, `column` and `row` use `{ ... }`. Whitespace and indentation no longer affect meaning, so code copied from chat assistants, web pages, or other editors works regardless of tabs and spaces. A whole page can be written on one line. Files using the old `page Home:` syntax get an error explaining how to convert them.
- **Error messages** now show the file, line and column, the source line with a `^` marker, and a hint. Several value errors are reported in one run; structural errors stop at the first one to avoid confusing follow-ups. Common mistakes get specific help: curly quotes, `16px`, `colour`, `paragraph`, misplaced properties, and a likely missing `}` when a `page` appears inside a block.
- **Padding precedence** is now deterministic: specific sides beat `block`/`inline`, which beat `padding`, regardless of source order.
- **CLI.** The compiler is now a command-line tool with subcommands instead of always building `lead.gns` from the current folder.
- **Default stylesheet** rewritten: responsive rows, alignment classes, link and button styles, image sizing, and no horizontal overflow on small screens.
- **Generated HTML** is indented and identical for identical programs, regardless of source formatting.

### Added

- MIT license (`LICENSE`, also included in the VS Code extension).
- Elements: `link`, `button`, `image`.
- Properties: `title`, `color` and `background` on pages, `level` and `align` on headings, `align` on text and layouts, `background` on buttons, `to`, `alt`, `width` on images. Percentage widths may have up to two decimal places.
- Multiple pages in one file (`index.html` plus `<name>.html`).
- `assets/` folder copied to `dist/assets/`; local images are checked to exist.
- Comments with `#`, optional `;` statement separators.
- Commands: `genesis build`, `genesis dev` (localhost server, automatic rebuild, page reload, errors shown in the browser), `genesis check` (with `--stdin` and `--machine` for editors), `genesis fmt`, `genesis init`, `genesis help`, `genesis --version`.
- Warnings for empty blocks, empty text, and images without `alt`.
- Test suite: lexer, parser, code generation, formatter, dev-server path safety, golden files, fuzzing, CLI integration, documentation examples, and VS Code extension tests.
- Documentation: README, tutorial, language reference, CLI reference, architecture notes, versioning policy, release checklist, contributing guide.
- GitHub Actions workflow for Linux, macOS and Windows, all running the same `make test` release gate.
- VS Code extension rewritten: compiler-backed diagnostics, context-aware completion, hovers, snippets, formatting, build and dev-server commands.

### Fixed

Found while stabilizing 0.9.0 (each has a regression test):

- `tests/cli_test.sh`, the `Makefile` and several sources had CRLF line endings, which broke the CLI tests under Unix shells (`set: Illegal option -` with `dash`). All text files are now LF, `.gitattributes` enforces LF on checkout regardless of `core.autocrlf`, and `make test` fails if a CRLF file appears (`tests/check_line_endings.sh`).
- The extension tests picked `genesis.exe` before `genesis` on every platform, so on Linux they could run a stale Windows binary. They now honour `GENESIS_BIN` first (and fail if it is invalid), otherwise use only the native binary name, and verify it runs. `make test` passes the freshly built compiler explicitly.
- The dev server closed connections while unread request data remained, so clients could receive a TCP reset instead of the response (seen with oversized headers, which should get `431`). Connections are now closed gracefully, with a bounded drain.
- An invalid link destination (for example `javascript:`) also produced a second, misleading "needs a destination" error.
- The VS Code extension showed a clean file when the compiler failed without producing diagnostics (crash, wrong binary). It now reports the failure in the *Genesis+* output channel and status bar.

Found in the earlier development builds:

- Using `width` on a column or row made the compiler hang forever (the property was recognised but never consumed).
- Files indented with spaces were rejected, and blank lines containing spaces could break parsing.
- `text ... color = ...` wrote the color into the HTML without escaping it.
- Some output writes (padding styles) were not checked for errors, and pages were written in place, so a failed write could leave a partially written page behind. Output is now generated in memory and written atomically.
- The old parser printed a "debug: unexpected token type" message instead of a real error for unknown statements.
- Errors for heading size and gap did not include a column number.

### Removed

- Indentation-based blocks (`:` followed by an indented body).
