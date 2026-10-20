# Genesis+ 0.9.0 release notes (draft)

> Draft for the GitHub release. Not yet published.

Genesis+ 0.9.0 is the first public testing release of a small language for building websites. Write a page as an outline of headings, text, rows, columns, links, buttons and images; Genesis+ produces clean, responsive HTML and CSS.

```gns
page Home {
    column gap = 16 padding = 32 background = "#f5f3ff" {
        heading "Welcome to Genesis+" color = "#7c3aed"
        text "Build websites without fighting your tools."
        button "Get started" to = "#start"
    }
}
```

This is a testing release: please try it on real pages and report anything confusing or broken. The language may still change before 1.0, and any breaking change will come with migration notes.

## Highlights

- **Braces instead of indentation.** Copy and paste code from anywhere; tabs, spaces and line breaks never change what a program means.
- **Errors that explain themselves**, with the exact location, the offending line, and a hint.
- **A complete workflow:** `genesis init`, `genesis check`, `genesis build`, `genesis dev` (automatic rebuild and browser reload), and `genesis fmt`.
- **Real websites:** multiple pages, links, buttons, images, responsive rows that stack on phones.
- **VS Code extension** (run from the repository) with live error checking powered by the compiler, completion, snippets and formatting.

## Upgrading from development builds

Replace every `:` that starts a block with `{`, and add a matching `}` after the block's last line:

```
page Home:                 page Home {
    column:          ->        column {
        text "Hi"                  text "Hi"
                               }
                           }
```

The compiler points at each old-style `:` with instructions.

## Installation

Build from source with GCC (or Clang) and Make; see the README. Prebuilt binaries are only part of this release if they are attached to this page.

## License

Genesis+ is released under the MIT License.

## Known limitations

See the README's *Known limitations* section.
