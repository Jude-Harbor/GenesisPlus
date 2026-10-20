#include "codegen.h"

#include <stdio.h>
#include <string.h>

#include "version.h"

void codegen_escape_html(Buffer *out, const char *text)
{
    for (const char *p = text; *p != '\0'; p++) {
        switch (*p) {
        case '&':  buffer_append_str(out, "&amp;"); break;
        case '<':  buffer_append_str(out, "&lt;"); break;
        case '>':  buffer_append_str(out, "&gt;"); break;
        case '"':  buffer_append_str(out, "&quot;"); break;
        case '\'': buffer_append_str(out, "&#39;"); break;
        default:   buffer_append_char(out, *p); break;
        }
    }
}

static void indent(Buffer *out, int depth)
{
    for (int i = 0; i < depth; i++) {
        buffer_append_str(out, "  ");
    }
}

/* Collects "name: value" declarations for an inline style attribute. All
   values written here are numbers or validated colors, never raw user text,
   so they cannot break out of the attribute or inject extra CSS. */
typedef struct {
    Buffer css;
} Style;

static void style_init(Style *style)
{
    buffer_init(&style->css);
}

static void style_add(Style *style, const char *name, const char *value)
{
    if (style->css.length > 0) {
        buffer_append_str(&style->css, "; ");
    }
    buffer_appendf(&style->css, "%s: %s", name, value);
}

static void style_add_px(Style *style, const char *name, int px)
{
    if (style->css.length > 0) {
        buffer_append_str(&style->css, "; ");
    }
    buffer_appendf(&style->css, "%s: %dpx", name, px);
}

static void format_percent(char *out, size_t size, int hundredths)
{
    int whole = hundredths / 100;
    int fraction = hundredths % 100;

    if (fraction == 0) {
        snprintf(out, size, "%d%%", whole);
    } else if (fraction % 10 == 0) {
        snprintf(out, size, "%d.%d%%", whole, fraction / 10);
    } else {
        snprintf(out, size, "%d.%02d%%", whole, fraction);
    }
}

/* Writes ` style="..."` if any declarations were collected, then frees. */
static void style_finish(Style *style, Buffer *out)
{
    if (style->css.length > 0) {
        buffer_append_str(out, " style=\"");
        buffer_append(out, style->css.data, style->css.length);
        buffer_append_str(out, "\"");
    }
    if (style->css.failed) {
        out->failed = 1;
    }
    buffer_free(&style->css);
}

static const char *align_class(AstAlign align)
{
    switch (align) {
    case ALIGN_LEFT:   return "align-left";
    case ALIGN_CENTER: return "align-center";
    case ALIGN_RIGHT:  return "align-right";
    default:           return NULL;
    }
}

static const char *align_value(AstAlign align)
{
    switch (align) {
    case ALIGN_LEFT:   return "left";
    case ALIGN_CENTER: return "center";
    case ALIGN_RIGHT:  return "right";
    default:           return NULL;
    }
}

static void add_text_styles(Style *style, const AstNode *node)
{
    if (node->size > 0) {
        style_add_px(style, "font-size", node->size);
    }
    if (node->color.set) {
        style_add(style, "color", node->color.value);
    }
    if (node->background.set) {
        style_add(style, "background-color", node->background.value);
    }
    if (align_value(node->align) != NULL) {
        style_add(style, "text-align", align_value(node->align));
    }
}

static void write_nodes(Buffer *out, const AstNode *first, int depth);

static void write_layout(Buffer *out, const AstNode *node, int depth)
{
    const char *kind = node->type == AST_ROW ? "row" : "column";
    const char *align = align_class(node->align);

    indent(out, depth);
    buffer_appendf(out, "<div class=\"%s%s%s%s\"", kind, align ? " " : "",
                   align ? align : "", node->has_width ? " sized" : "");

    Style style;
    style_init(&style);

    if (node->background.set) {
        style_add(&style, "background-color", node->background.value);
    }
    if (node->has_gap) {
        style_add_px(&style, "gap", node->gap);
    }
    if (node->has_width) {
        char width[16];
        format_percent(width, sizeof(width), node->width_hundredths);
        style_add(&style, "width", width);
    }

    /* Least to most specific, so the more specific value wins. */
    static const char *const padding_names[PAD_COUNT] = {
        "padding", "padding-block", "padding-inline", "padding-top",
        "padding-bottom", "padding-left", "padding-right",
    };

    for (int side = 0; side < PAD_COUNT; side++) {
        if (node->has_padding[side]) {
            style_add_px(&style, padding_names[side], node->padding[side]);
        }
    }

    style_finish(&style, out);

    if (node->first_child == NULL) {
        buffer_append_str(out, "></div>\n");
        return;
    }

    buffer_append_str(out, ">\n");
    write_nodes(out, node->first_child, depth + 1);
    indent(out, depth);
    buffer_append_str(out, "</div>\n");
}

static void write_text_element(Buffer *out, const AstNode *node, int depth,
                               const char *tag)
{
    indent(out, depth);
    buffer_appendf(out, "<%s", tag);

    Style style;
    style_init(&style);
    add_text_styles(&style, node);
    style_finish(&style, out);

    buffer_append_char(out, '>');
    codegen_escape_html(out, node->value);
    buffer_appendf(out, "</%s>\n", tag);
}

static void write_anchor(Buffer *out, const AstNode *node, int depth)
{
    indent(out, depth);
    buffer_append_str(out, node->type == AST_BUTTON ? "<a class=\"button\" href=\"" : "<a href=\"");
    codegen_escape_html(out, node->href != NULL ? node->href : "#");
    buffer_append_char(out, '"');

    Style style;
    style_init(&style);
    add_text_styles(&style, node);
    style_finish(&style, out);

    buffer_append_char(out, '>');
    codegen_escape_html(out, node->value);
    buffer_append_str(out, "</a>\n");
}

static void write_image(Buffer *out, const AstNode *node, int depth)
{
    indent(out, depth);
    buffer_append_str(out, node->has_width ? "<img class=\"sized\" src=\"" : "<img src=\"");
    codegen_escape_html(out, node->value);
    buffer_append_str(out, "\" alt=\"");
    codegen_escape_html(out, node->alt != NULL ? node->alt : "");
    buffer_append_char(out, '"');

    Style style;
    style_init(&style);
    if (node->has_width) {
        char width[16];
        format_percent(width, sizeof(width), node->width_hundredths);
        style_add(&style, "width", width);
    }
    style_finish(&style, out);

    buffer_append_str(out, ">\n");
}

static void write_nodes(Buffer *out, const AstNode *first, int depth)
{
    for (const AstNode *node = first; node != NULL; node = node->next_sibling) {
        switch (node->type) {
        case AST_COLUMN:
        case AST_ROW:
            write_layout(out, node, depth);
            break;
        case AST_HEADING: {
            char tag[3] = {'h', (char)('0' + (node->level >= 1 && node->level <= 6 ? node->level : 1)), '\0'};
            write_text_element(out, node, depth, tag);
            break;
        }
        case AST_TEXT:
            write_text_element(out, node, depth, "p");
            break;
        case AST_LINK:
        case AST_BUTTON:
            write_anchor(out, node, depth);
            break;
        case AST_IMAGE:
            write_image(out, node, depth);
            break;
        case AST_PROGRAM:
        case AST_PAGE:
            /* Never nested; the parser rejects this. */
            break;
        }
    }
}

void codegen_page_html(const AstNode *page, Buffer *out)
{
    buffer_append_str(out,
                      "<!doctype html>\n"
                      "<html lang=\"en\">\n"
                      "<head>\n"
                      "  <meta charset=\"utf-8\">\n"
                      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                      "  <meta name=\"generator\" content=\"Genesis+ " GENESIS_VERSION "\">\n"
                      "  <title>");
    codegen_escape_html(out, page->title != NULL ? page->title : page->value);
    buffer_append_str(out,
                      "</title>\n"
                      "  <link rel=\"stylesheet\" href=\"style.css\">\n"
                      "</head>\n"
                      "<body");

    Style style;
    style_init(&style);
    if (page->background.set) {
        style_add(&style, "background-color", page->background.value);
    }
    if (page->color.set) {
        style_add(&style, "color", page->color.value);
    }
    style_finish(&style, out);

    buffer_append_str(out, ">\n  <main>\n");
    write_nodes(out, page->first_child, 2);
    buffer_append_str(out, "  </main>\n</body>\n</html>\n");
}

void codegen_css(Buffer *out)
{
    buffer_append_str(out,
        "/* Generated by Genesis+ " GENESIS_VERSION ". This file is replaced on every build. */\n"
        "\n"
        "*,\n"
        "*::before,\n"
        "*::after {\n"
        "  box-sizing: border-box;\n"
        "}\n"
        "\n"
        "html {\n"
        "  -webkit-text-size-adjust: 100%;\n"
        "  text-size-adjust: 100%;\n"
        "}\n"
        "\n"
        "body {\n"
        "  margin: 0;\n"
        "  font-family: system-ui, -apple-system, \"Segoe UI\", Roboto, \"Helvetica Neue\",\n"
        "    Arial, sans-serif;\n"
        "  font-size: 1.0625rem;\n"
        "  line-height: 1.6;\n"
        "  color: #202124;\n"
        "  background-color: #ffffff;\n"
        "}\n"
        "\n"
        "/* The page content. Children are stacked like a column. */\n"
        "main {\n"
        "  display: flex;\n"
        "  flex-direction: column;\n"
        "  gap: 1.5rem;\n"
        "  width: 100%;\n"
        "  max-width: 960px;\n"
        "  margin-inline: auto;\n"
        "  padding: 2.5rem 1.25rem;\n"
        "}\n"
        "\n"
        "/* Layouts */\n"
        ".column,\n"
        ".row {\n"
        "  display: flex;\n"
        "  gap: 1rem;\n"
        "  min-width: 0;\n"
        "}\n"
        "\n"
        ".column {\n"
        "  flex-direction: column;\n"
        "}\n"
        "\n"
        ".row {\n"
        "  flex-direction: row;\n"
        "}\n"
        "\n"
        "/* Row children share the space equally unless they have a width. */\n"
        ".row > * {\n"
        "  flex: 1 1 0;\n"
        "  min-width: 0;\n"
        "}\n"
        "\n"
        ".row > .sized {\n"
        "  flex: 0 1 auto;\n"
        "}\n"
        "\n"
        ".row > a {\n"
        "  flex: 0 0 auto;\n"
        "}\n"
        "\n"
        "/* Alignment */\n"
        ".align-left {\n"
        "  text-align: left;\n"
        "}\n"
        "\n"
        ".align-center {\n"
        "  text-align: center;\n"
        "}\n"
        "\n"
        ".align-right {\n"
        "  text-align: right;\n"
        "}\n"
        "\n"
        ".row.align-center {\n"
        "  justify-content: center;\n"
        "}\n"
        "\n"
        ".row.align-right {\n"
        "  justify-content: flex-end;\n"
        "}\n"
        "\n"
        ".column.align-center > a,\n"
        ".column.align-center > img {\n"
        "  margin-inline: auto;\n"
        "}\n"
        "\n"
        ".column.align-right > a,\n"
        ".column.align-right > img {\n"
        "  margin-left: auto;\n"
        "}\n"
        "\n"
        "/* Text */\n"
        "h1,\n"
        "h2,\n"
        "h3,\n"
        "h4,\n"
        "h5,\n"
        "h6 {\n"
        "  margin: 0;\n"
        "  font-weight: 700;\n"
        "  line-height: 1.2;\n"
        "  letter-spacing: -0.02em;\n"
        "  overflow-wrap: anywhere;\n"
        "}\n"
        "\n"
        "h1 {\n"
        "  font-size: clamp(2rem, 1.4rem + 3vw, 3.25rem);\n"
        "}\n"
        "\n"
        "h2 {\n"
        "  font-size: clamp(1.6rem, 1.25rem + 1.8vw, 2.4rem);\n"
        "}\n"
        "\n"
        "h3 {\n"
        "  font-size: 1.5rem;\n"
        "}\n"
        "\n"
        "h4 {\n"
        "  font-size: 1.25rem;\n"
        "}\n"
        "\n"
        "h5 {\n"
        "  font-size: 1.1rem;\n"
        "}\n"
        "\n"
        "h6 {\n"
        "  font-size: 1rem;\n"
        "}\n"
        "\n"
        "p {\n"
        "  margin: 0;\n"
        "  overflow-wrap: anywhere;\n"
        "}\n"
        "\n"
        "/* Links and buttons */\n"
        "a {\n"
        "  width: fit-content;\n"
        "  max-width: 100%;\n"
        "  color: #6d28d9;\n"
        "  text-underline-offset: 0.15em;\n"
        "  overflow-wrap: anywhere;\n"
        "}\n"
        "\n"
        "a:hover {\n"
        "  color: #5b21b6;\n"
        "}\n"
        "\n"
        "a:focus-visible {\n"
        "  outline: 3px solid #a78bfa;\n"
        "  outline-offset: 2px;\n"
        "}\n"
        "\n"
        ".button {\n"
        "  display: inline-block;\n"
        "  padding: 0.7em 1.4em;\n"
        "  border-radius: 0.5rem;\n"
        "  background-color: #7c3aed;\n"
        "  color: #ffffff;\n"
        "  font-weight: 600;\n"
        "  line-height: 1.2;\n"
        "  text-align: center;\n"
        "  text-decoration: none;\n"
        "}\n"
        "\n"
        ".button:hover {\n"
        "  color: #ffffff;\n"
        "  filter: brightness(1.1);\n"
        "}\n"
        "\n"
        "/* Images */\n"
        "img {\n"
        "  display: block;\n"
        "  max-width: 100%;\n"
        "  height: auto;\n"
        "}\n"
        "\n"
        "/* Small screens: rows stack vertically and their sized children use the full width. */\n"
        "@media (max-width: 640px) {\n"
        "  main {\n"
        "    padding: 1.5rem 1rem;\n"
        "  }\n"
        "\n"
        "  .row {\n"
        "    flex-direction: column;\n"
        "  }\n"
        "\n"
        "  .row > * {\n"
        "    flex: 0 0 auto;\n"
        "  }\n"
        "\n"
        "  .row > .sized {\n"
        "    width: 100% !important;\n"
        "  }\n"
        "\n"
        "  .row.align-center {\n"
        "    align-items: center;\n"
        "  }\n"
        "\n"
        "  .row.align-right {\n"
        "    align-items: flex-end;\n"
        "  }\n"
        "}\n");
}
