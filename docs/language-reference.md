# Genesis+ language reference

This document describes the Genesis+ 0.9 language exactly as the compiler implements it. Every `gns` example in this file is compiled by the test suite.

- [Source files](#source-files)
- [Structure: pages and blocks](#structure-pages-and-blocks)
- [Statements and whitespace](#statements-and-whitespace)
- [Values](#values)
- [Elements](#elements)
- [Properties](#properties)
- [Layout and styling rules](#layout-and-styling-rules)
- [Generated output](#generated-output)
- [Errors and warnings](#errors-and-warnings)
- [Grammar](#grammar)

## Source files

- A Genesis+ program is a single UTF-8 text file with the extension `.gns`. By convention the main file of a project is called `lead.gns`.
- Windows (`CRLF`), Unix (`LF`) and old Mac (`CR`) line endings are all accepted. A UTF-8 byte order mark at the start of the file is ignored.
- Comments start with `#` and run to the end of the line. A `#` inside a string (such as a color) is not a comment.
- Keywords and property names are lowercase and case-sensitive: `page` is a keyword, `Page` is not.

```gns
# This is a comment.
page Home {
    text "A # inside quotes is just text."  # a trailing comment
}
```

## Structure: pages and blocks

A file contains one or more **pages**. Each page has a name and a **block**: a pair of braces `{ }` that holds the page's content.

```gns
page Home {
    heading "Welcome"
}

page About {
    text "About us"
}
```

`column` and `row` also have blocks, and can be nested as deeply as you like (up to 64 levels). `heading`, `text`, `link`, `button` and `image` cannot contain other elements.

### Page names and output files

- Page names are made of letters, digits and `_`, start with a letter or `_`, and are at most 64 characters long.
- The **first page** in the file is written to `index.html`, whatever its name.
- Every other page is written to its name in lowercase plus `.html`: `page About` becomes `about.html`, `page Contact_Us` becomes `contact_us.html`.
- Page names must be unique, ignoring case.
- A page other than the first cannot be called `Index` (it would clash with `index.html`).
- Names reserved by Windows (`con`, `prn`, `aux`, `nul`, `com1`–`com9`, `lpt1`–`lpt9`) cannot be used.
- Link between pages with their file names: `link "About" to = "about.html"`.

## Statements and whitespace

Whitespace (spaces, tabs and line breaks) only separates words. **Indentation never changes the meaning of a program.** These two files are identical to the compiler:

```gns
page Home {
    column {
        heading "Welcome!"
        text "Hello!"
    }
}
```

```gns
page Home{column{heading "Welcome!" text "Hello!"}}
```

Each statement starts with an element keyword (`column`, `heading`, `text`, ...), so the compiler always knows where one statement ends and the next begins. You do not need semicolons, but you may end a statement with `;` if you prefer. Properties that continue on the next line still belong to the element before them:

```gns
page Home {
    heading "A long heading"
        size = 40
        color = "#7c3aed"
}
```

Invisible characters that often appear in copied code — non-breaking spaces (U+00A0, U+202F), zero-width spaces (U+200B) and stray byte order marks — are treated as spaces.

## Values

| Type | Written as | Examples | Notes |
| --- | --- | --- | --- |
| String | text in straight double quotes | `"Hello"`, `""` | Cannot span lines. Use `\"` for a quote and `\\` for a backslash. No other escapes exist. Curly quotes (`“ ”`) are not accepted. |
| Whole number | digits | `16`, `0`, `1000` | No sign, no decimal point, no unit. `16px` is an error. |
| Percentage | number followed directly by `%` | `50%`, `33.33%` | Up to two decimal places. No space before `%`. |
| Color | a string containing a hex color | `"#7c3aed"`, `"#fff"` | Exactly `#RGB` or `#RRGGBB`. Upper- or lowercase hex digits. |
| Alignment | a string | `"left"`, `"center"`, `"right"` | Lowercase only. |

Text is used exactly as written: characters such as `<`, `&` and `"` are shown as text, never interpreted as HTML. Any Unicode text, including emoji, can appear inside strings.

```gns
page Home {
    text "Quotes look like \"this\", and <b> is not bold."
    text "Café ☕ — 日本語"
}
```

## Elements

### `page`

```
page NAME [properties] { ... }
```

A web page. Properties: `title`, `background`, `color`.

```gns
page Home title = "My website" background = "#fafafa" color = "#111827" {
    heading "Hello"
}
```

### `column`

```
column [properties] { ... }
```

Stacks its children vertically. Properties: `background`, `gap`, `width`, `padding` (and padding sides), `align`.

### `row`

```
row [properties] { ... }
```

Places its children side by side. Children share the row's width equally unless they have a `width`. On screens narrower than 640 pixels, rows stack their children vertically. Properties: the same as `column`.

```gns
page Home {
    row gap = 16 {
        column padding = 16 background = "#fee2e2" { text "One" }
        column padding = 16 background = "#dcfce7" { text "Two" }
    }
}
```

### `heading`

```
heading "TEXT" [properties]
```

A heading. Level 1 (`<h1>`) by default. Properties: `size`, `color`, `level`, `align`.

### `text`

```
text "TEXT" [properties]
```

A paragraph. Properties: `size`, `color`, `align`.

### `link`

```
link "TEXT" to = "DESTINATION" [properties]
```

A text link. `to` is required. Properties: `to`, `size`, `color`.

### `button`

```
button "TEXT" to = "DESTINATION" [properties]
```

A link that looks like a button. `to` is required. Properties: `to`, `size`, `color`, `background`.

```gns
page Home {
    link "Read the docs" to = "https://example.com/docs"
    button "Contact us" to = "mailto:hello@example.com" background = "#111827"
}
```

### `image`

```
image "SOURCE" [properties]
```

An image. Properties: `alt`, `width`.

The source is either a web address starting with `https://` or `http://`, or a file inside the project's `assets` folder, written with forward slashes: `"assets/photo.jpg"` or `"assets/team/alex.png"`. Local files must exist when you build; the whole `assets` folder is copied to `dist/assets/`. Paths cannot contain `..`, `\`, `:` or empty folder names.

Always describe the image with `alt` for people who use screen readers; leaving it out produces a warning. Use `alt = ""` for purely decorative images.

```gns
page Home {
    image "https://example.com/photo.jpg" alt = "Our shop on a sunny day" width = 50%
}
```

## Properties

Properties are written as `name = value` after the element (and its text, if it has one) and before its `{` (if it has a block). They can be in any order. Each property may appear only once per element.

| Property | Value | Allowed on | Meaning | Default |
| --- | --- | --- | --- | --- |
| `title` | string, not empty | page | Browser tab title (`<title>`) | the page name |
| `background` | color | page, column, row, button | Background color | page: white; button: `#7c3aed`; others: none |
| `color` | color | page, heading, text, link, button | Text color | page: `#202124`; links: `#6d28d9`; buttons: white |
| `size` | whole number 8–1000 | heading, text, link, button | Font size in pixels | depends on the element |
| `level` | whole number 1–6 | heading | Heading level (`<h1>`–`<h6>`) | 1 |
| `align` | `"left"`, `"center"`, `"right"` | column, row, heading, text | Horizontal alignment | left |
| `gap` | whole number 0–256 | column, row | Space between children, in pixels | 16 pixels (`1rem`) |
| `width` | percentage, more than 0% and at most 100% | column, row, image | Width as a share of the available space | column: full width; row child: equal share; image: natural size |
| `padding` | whole number 0–256 | column, row | Space inside the block, in pixels | 0 |
| `to` | link destination | link, button (required) | Where the link goes | — |
| `alt` | string | image | Image description | empty (with a warning) |

### Padding sides

`padding` can be followed by a side to set only part of the padding:

| Written as | Sets |
| --- | --- |
| `padding = N` | all four sides |
| `padding block = N` | top and bottom |
| `padding inline = N` | left and right |
| `padding top = N`, `padding bottom = N`, `padding left = N`, `padding right = N` | one side |

When several apply to the same side, **the more specific one wins, regardless of the order you write them in**: a single side beats `block`/`inline`, which beat plain `padding`.

```gns
page Home {
    column padding = 40 padding top = 8 background = "#fae8ff" {
        text "40 pixels on the left, right and bottom; 8 at the top."
    }
}
```

Different sides are different properties, so `padding = 4 padding top = 8` is allowed, but `padding top = 4 padding top = 8` is a duplicate.

### Link destinations (`to`)

Allowed:

- pages and files in your site: `"about.html"`, `"docs/"`, `"assets/menu.pdf"`;
- sections: `"#contact"`;
- web addresses: `"https://example.com"`, `"http://example.com"`;
- email and phone links: `"mailto:hello@example.com"`, `"tel:+15550100"`.

Not allowed: any other scheme (such as `javascript:`, `data:` or `file:`), spaces (use `%20`), backslashes, control characters, and the characters `"`, `<`, `>` and `` ` ``.

## Layout and styling rules

- Every page's content is centered in a column up to 960 pixels wide, with comfortable margins. Children of the page are stacked with a 24-pixel gap.
- `column` and `row` have a default gap of 16 pixels. `gap = 0` removes it.
- In a `row`, children without a `width` share the space equally. Children with a `width` take that share of the row and shrink if needed to fit next to the gaps, so they never overflow. Links and buttons in a row keep their natural width.
- In a `column`, a child with a `width` takes that share of the column's width.
- Below 640 pixels wide, every row stacks its children vertically, and row children with a `width` use the full width.
- `align` on `heading` and `text` sets text alignment. On a `column` it aligns the text inside it and centers or right-aligns its links, buttons and images. On a `row` it moves the row's children to the center or right, and aligns text the same way.
- An element's own `align` overrides the alignment inherited from its column or row.
- Long words and URLs wrap instead of making the page wider than the screen.
- Level 1 and 2 headings get smaller on narrow screens unless you set `size`.

## Generated output

`genesis build` writes these files to a `dist` folder next to the source file:

- one HTML file per page (`index.html` for the first page);
- `style.css`, always generated, and identical for every project (element-specific styles are written as inline `style` attributes);
- `assets/`, a copy of the project's `assets` folder, if there is one.

The output depends only on the meaning of the program, never on its formatting, and the same input always produces byte-for-byte the same output. Nothing is written if there are any errors. The compiler never deletes files from `dist/`, so a renamed page leaves its old file behind.

## Errors and warnings

Errors stop the build; warnings do not. Each message shows the file, line and column, the source line with a `^` marker, and usually a hint:

```
Genesis+ error in lead.gns at line 4, column 25:
  Invalid width '120%'. Width must be more than 0% and at most 100%.

    4 |     column width = 120% {
      |                    ^^^^
  Hint: Width is a share of the available space, e.g. width = 50%.
```

Problems with property values are all reported in one run (up to 32 messages). Structural problems, such as a missing brace, stop the compiler at the first one, because anything after it would be guesswork.

### Errors

| Situation | Example message |
| --- | --- |
| Old `:` block syntax | `Genesis+ now uses braces instead of ':' and indentation to start a page block.` |
| Missing `}` | `The column block that starts here is never closed: expected '}' before the end of the file.` |
| Extra `}` | `Unexpected '}': there is no open block to close here.` |
| Missing `{` | `Expected '{' to start the column's content, but found 'text'.` |
| A page inside a block (usually a missing `}`) | `Unexpected 'page' inside a column.` |
| Content outside a page | `'heading' must be inside a page.` |
| Empty file | `This file has no pages.` |
| Unknown element | `Unknown element 'paragraph'.` (with a suggestion such as `text`) |
| Unknown property | `Unknown property 'colour' on heading.` (with `Did you mean 'color'?`) |
| Property not valid for the element | `'gap' cannot be used on heading.` |
| Property after `{` | `The property 'gap' is in the wrong place.` |
| Property name without `=` | `Expected '=' after 'size'.` |
| Missing value | `Expected a value after 'size =', but found '}'.` |
| Unquoted text value | `The value 'center' must be in double quotes (or be a number).` |
| Wrong value type | `'size' needs a whole number, like size = 24, but found the text "big".` |
| Number out of range | `Invalid size 2000. 'size' must be between 8 and 1000 (pixels).` |
| Invalid width | `Invalid width '120%'. Width must be more than 0% and at most 100%.` |
| Invalid color | `Invalid color "red". Expected #RGB or #RRGGBB.` |
| Invalid alignment | `Invalid align value "middle".` |
| Duplicate property | `Duplicate property 'size'.` |
| Unknown padding side | `Unknown padding side 'up'.` |
| Element without its text | `Expected text in double quotes after 'heading', but found '}'.` |
| Content element with a block | `A text cannot contain other elements.` |
| Link or button without `to` | `This link needs a destination: add to = "...".` |
| Disallowed link scheme | `Links starting with 'javascript:' are not allowed.` |
| Invalid link destination | `Invalid link destination. The destination contains a space. ...` |
| Image outside `assets` | `Local images must be inside the assets folder.` |
| Image file missing | `Image file 'assets/cat.jpg' was not found.` |
| Empty page title | `The page title cannot be empty.` |
| Page name problems | `Duplicate page name 'home'.`, `Only the first page can be called 'Index'.` |
| Nesting deeper than 64 | `Blocks are nested too deeply (the limit is 64 levels).` |
| Unterminated string | `This string is missing its closing quote.` |
| Unknown escape | `Unknown escape sequence '\n'.` |
| Curly quotes | `Found a curly quote (“). Genesis+ strings use straight quotes.` |
| Units on numbers | `Unexpected unit in '16px'.` |
| Negative numbers | `Negative values like '-4' are not allowed.` |
| Other characters | `Unexpected character '@'.` |

### Warnings

| Situation | Message |
| --- | --- |
| `column`, `row` or `page` with nothing inside | `This column is empty.` |
| `heading`, `text`, `link` or `button` with empty text | `This text is empty.` |
| `image` without `alt` | `This image has no 'alt' description.` |

## Grammar

The complete grammar in EBNF. `{ x }` means zero or more `x`; `[ x ]` means optional.

```
program     = page { page } ;
page        = "page" NAME { property } block ;
block       = "{" { statement } "}" ;
statement   = layout | content | ";" ;
layout      = ( "column" | "row" ) { property } block ;
content     = ( "heading" | "text" | "link" | "button" | "image" ) STRING { property } ;
property    = PROPERTY_NAME [ PADDING_SIDE ] "=" value ;
value       = STRING | NUMBER | PERCENTAGE ;

NAME          = letter { letter | digit } ;          (* letter includes "_" *)
PROPERTY_NAME = "title" | "background" | "color" | "size" | "level" | "align"
              | "gap" | "width" | "padding" | "to" | "alt" ;
PADDING_SIDE  = "top" | "bottom" | "left" | "right" | "inline" | "block" ;
STRING        = '"' { character | '\"' | '\\' } '"' ;   (* on one line *)
NUMBER        = digit { digit } [ "." digit { digit } ] ;
PERCENTAGE    = NUMBER "%" ;
COMMENT       = "#" { any character except a line break } ;
```

`PADDING_SIDE` is only allowed after `padding`. Which properties each element accepts, and the allowed ranges, are listed in [Properties](#properties). Reserved words (`page`, `column`, `row`, `heading`, `text`, `link`, `button`, `image`) cannot be used as page names.
