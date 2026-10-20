# Tutorial: build your first website

In this tutorial you will build a small website for a fictional photographer, Maya, from an empty folder to a finished site with two pages. It takes about 20 minutes. You do not need to know HTML or CSS.

You will need the `genesis` compiler. If you have not built it yet, follow [Installation](../README.md#installation) first. The commands below assume `genesis` is on your `PATH`; if it is not, type the full path to `genesis.exe` instead.

## 1. Create a project

Open a terminal (on Windows, the MSYS2 UCRT64 terminal, PowerShell, or the VS Code terminal all work) and run:

```bash
genesis init maya-site
cd maya-site
```

This creates three things:

- `lead.gns`, the source of your website, with a small starter page;
- `assets/`, a folder for images;
- `.gitignore`, which tells Git not to save the generated `dist/` folder.

## 2. Start the live preview

```bash
genesis dev
```

You will see something like:

```
Built lead.gns -> ./dist/
  index.html
  style.css
Ready. Open pages reload automatically.

Genesis+ 0.9.0 dev server
  Open     http://localhost:3000/
```

Open <http://localhost:3000/> in your browser. Leave the terminal running: every time you save `lead.gns`, the site is rebuilt and the page in your browser reloads by itself.

> If port 3000 is busy, run `genesis dev --port 3001` and open `http://localhost:3001/` instead.

## 3. Start from a blank page

Open `lead.gns` in your editor. If you use VS Code, install the Genesis+ extension (see [its README](../vscode-extension/README.md)) to get colors, completion and live error checking.

Replace everything in the file with:

```gns
page Home title = "Maya Lens Photography" {
    heading "Maya Lens"
    text "Wedding and portrait photography in Lisbon."
}
```

Save. The browser shows a heading and a line of text.

What you wrote:

- `page Home { ... }` is a page. Everything between the braces is its content. The first page becomes `index.html`.
- `title = "..."` sets the text shown on the browser tab.
- `heading` and `text` are elements. Their text goes in double quotes.

The indentation is only there to make the file easy to read. Genesis+ uses the braces to understand the structure, so it does not matter if you use tabs, spaces, or no indentation at all.

## 4. Add a hero section

A `column` stacks things vertically. Give it a background color, some padding (space inside) and a gap (space between items):

```gns
page Home title = "Maya Lens Photography" background = "#fafaf9" {
    column gap = 16 padding = 48 background = "#1c1917" align = "center" {
        text "WEDDINGS - PORTRAITS - EVENTS" size = 14 color = "#a8a29e"
        heading "Photos you will want to keep" color = "#ffffff"
        text "Natural, unposed photography in Lisbon and beyond." color = "#e7e5e4" size = 19
        button "Book a session" to = "#contact" background = "#f59e0b" color = "#1c1917"
    }
}
```

Save and look at the browser. Notice:

- Colors are hex codes in quotes, like `"#f59e0b"`. A color picker in your editor or browser will give you these codes.
- `size` is the font size in pixels.
- `align = "center"` centers the text and the button.
- A `button` needs a destination: `to = "..."`. `#contact` will jump to a section we add later; for now it does nothing.

## 5. Make a mistake on purpose

Delete the closing `}` of the column, then save. The browser now shows the error instead of the page, and the terminal says:

```
Genesis+ error in lead.gns at line 1, column ...:
  The page block that starts here is never closed: expected '}' before the end of the file.
```

The message tells you where the problem is and how to fix it. Put the `}` back and save; the page returns. Try a few more mistakes to see the messages, for example `size = 16px` or `color = "red"`.

## 6. Show your services side by side

A `row` puts things next to each other. Inside it, use three columns as cards. Add this after the hero column, inside the page:

```gns
page Home title = "Maya Lens Photography" background = "#fafaf9" {
    heading "What I photograph" level = 2 align = "center"

    row gap = 16 {
        column gap = 8 padding = 24 background = "#ffffff" {
            heading "Weddings" level = 3
            text "The whole day, from getting ready to the last dance."
        }
        column gap = 8 padding = 24 background = "#ffffff" {
            heading "Portraits" level = 3
            text "Relaxed sessions outdoors or in my studio."
        }
        column gap = 8 padding = 24 background = "#ffffff" {
            heading "Events" level = 3
            text "Birthdays, launches and everything in between."
        }
    }
}
```

`level = 2` and `level = 3` make smaller, lower-level headings. The three cards share the row equally. Make your browser window narrow, or open the page on your phone: the cards stack on top of each other.

## 7. Add an image

Copy a photo into the `assets` folder, for example `assets/portrait.jpg`. Then add an image with a description for people who use screen readers:

```gns-snippet
image "assets/portrait.jpg" alt = "A couple laughing on a tram in Lisbon" width = 60%
```

`width = 60%` makes the image take 60% of the available width. When you build, the whole `assets` folder is copied into the website. If the file name is wrong, Genesis+ tells you it cannot find the image.

## 8. Add a second page

Pages are listed one after another. Add an About page at the end of the file, after the last `}` of the Home page:

```gns
page About title = "About - Maya Lens" background = "#fafaf9" {
    link "Back to home" to = "index.html"
    heading "About me"
    text "I have been photographing people for ten years."
    column padding = 24 padding left = 32 background = "#fef3c7" {
        text "Fun fact: I shoot on film for every wedding, too."
    }
}
```

This page is saved as `about.html`. Link to it from the home page by adding this line inside the Home page, for example at the top:

```gns
page Home {
    row gap = 16 align = "right" {
        link "Home" to = "index.html"
        link "About" to = "about.html"
    }
}
```

`padding left = 32` adds extra space only on the left side. You can also use `top`, `bottom`, `right`, `inline` (left and right) and `block` (top and bottom).

## 9. Add a contact section

Finish the home page with a contact section. Add this at the end of the Home page:

```gns
page Home {
    column gap = 12 padding = 40 background = "#fef3c7" align = "center" {
        heading "Let's talk" level = 2
        text "Tell me about your plans and I will reply within a day."
        button "Email me" to = "mailto:maya@example.com"
    }
}
```

`mailto:` links open the visitor's email app.

## 10. Tidy up and build

Let Genesis+ format your file neatly:

```bash
genesis fmt
```

Then stop the dev server with `Ctrl+C` and build the final website:

```bash
genesis build
```

```
Built lead.gns -> ./dist/
  index.html
  about.html
  style.css
  assets/ (copied)
```

The `dist` folder is your finished website. To publish it, upload the contents of `dist` to any static web host, for example GitHub Pages, Netlify, or your own server.

## The finished file

```gns
page Home title = "Maya Lens Photography" background = "#fafaf9" {
    row gap = 16 align = "right" {
        link "Home" to = "index.html"
        link "About" to = "about.html"
    }

    column gap = 16 padding = 48 background = "#1c1917" align = "center" {
        text "WEDDINGS - PORTRAITS - EVENTS" size = 14 color = "#a8a29e"
        heading "Photos you will want to keep" color = "#ffffff"
        text "Natural, unposed photography in Lisbon and beyond." color = "#e7e5e4" size = 19
        button "Book a session" to = "#contact" background = "#f59e0b" color = "#1c1917"
    }

    heading "What I photograph" level = 2 align = "center"

    row gap = 16 {
        column gap = 8 padding = 24 background = "#ffffff" {
            heading "Weddings" level = 3
            text "The whole day, from getting ready to the last dance."
        }
        column gap = 8 padding = 24 background = "#ffffff" {
            heading "Portraits" level = 3
            text "Relaxed sessions outdoors or in my studio."
        }
        column gap = 8 padding = 24 background = "#ffffff" {
            heading "Events" level = 3
            text "Birthdays, launches and everything in between."
        }
    }

    image "assets/portrait.jpg" alt = "A couple laughing on a tram in Lisbon" width = 60%

    column gap = 12 padding = 40 background = "#fef3c7" align = "center" {
        heading "Let's talk" level = 2
        text "Tell me about your plans and I will reply within a day."
        button "Email me" to = "mailto:maya@example.com"
    }
}

page About title = "About - Maya Lens" background = "#fafaf9" {
    link "Back to home" to = "index.html"
    heading "About me"
    text "I have been photographing people for ten years."
    column padding = 24 padding left = 32 background = "#fef3c7" {
        text "Fun fact: I shoot on film for every wedding, too."
    }
}
```

## Next steps

- Read the [language reference](language-reference.md) for every element and property.
- Look at the [examples](../examples/) for more layouts.
