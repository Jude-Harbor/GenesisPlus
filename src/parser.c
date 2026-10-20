#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

/*
 * Grammar (see docs/language-reference.md for the full description):
 *
 *   program    = page { page }
 *   page       = "page" NAME { property } block
 *   block      = "{" { statement } "}"
 *   statement  = layout | content | ";"
 *   layout     = ( "column" | "row" ) { property } block
 *   content    = ( "heading" | "text" | "link" | "button" | "image" )
 *                STRING { property }
 *   property   = NAME [ SIDE ] "=" value
 *   value      = STRING | NUMBER | PERCENTAGE
 *
 * Statements are recognised by their leading keyword, so newlines and
 * indentation are never needed to tell them apart.
 *
 * Error handling: problems with a property's *value* (out of range, wrong
 * type, duplicates, unknown names) are reported and parsing continues, so
 * several can be reported at once. Structural problems (missing braces,
 * unexpected tokens) stop the parse immediately to avoid cascades of
 * confusing follow-up errors.
 */

typedef struct {
    Lexer lexer;
    Token current;
    Token next;
    DiagList *diags;
    int failed; /* a structural error occurred; stop parsing */
    int depth;
    int out_of_memory;
    unsigned properties_seen; /* bit per PropertyId written on the last element */
} Parser;

/* ------------------------------------------------------------------------ */
/* Property table                                                           */
/* ------------------------------------------------------------------------ */

typedef enum {
    VAL_INT,
    VAL_COLOR,
    VAL_ALIGN,
    VAL_PERCENT,
    VAL_STRING,
    VAL_URL
} ValueKind;

#define MASK(type) (1u << (type))
#define M_PAGE MASK(AST_PAGE)
#define M_LAYOUT (MASK(AST_COLUMN) | MASK(AST_ROW))
#define M_HEADING MASK(AST_HEADING)
#define M_TEXT MASK(AST_TEXT)
#define M_LINK MASK(AST_LINK)
#define M_BUTTON MASK(AST_BUTTON)
#define M_IMAGE MASK(AST_IMAGE)

typedef enum {
    PROP_TITLE,
    PROP_BACKGROUND,
    PROP_COLOR,
    PROP_SIZE,
    PROP_LEVEL,
    PROP_ALIGN,
    PROP_GAP,
    PROP_WIDTH,
    PROP_PADDING,
    PROP_TO,
    PROP_ALT,
    PROP_COUNT
} PropertyId;

typedef struct {
    PropertyId id;
    const char *name;
    unsigned elements;
    ValueKind kind;
    int min;
    int max;
    const char *example;
} PropertySpec;

static const PropertySpec PROPERTIES[PROP_COUNT] = {
    {PROP_TITLE, "title", M_PAGE, VAL_STRING, 0, 0, "title = \"My website\""},
    {PROP_BACKGROUND, "background", M_PAGE | M_LAYOUT | M_BUTTON, VAL_COLOR, 0, 0,
     "background = \"#f5f3ff\""},
    {PROP_COLOR, "color", M_PAGE | M_HEADING | M_TEXT | M_LINK | M_BUTTON, VAL_COLOR, 0, 0,
     "color = \"#7c3aed\""},
    {PROP_SIZE, "size", M_HEADING | M_TEXT | M_LINK | M_BUTTON, VAL_INT, 8, 1000,
     "size = 24"},
    {PROP_LEVEL, "level", M_HEADING, VAL_INT, 1, 6, "level = 2"},
    {PROP_ALIGN, "align", M_LAYOUT | M_HEADING | M_TEXT, VAL_ALIGN, 0, 0,
     "align = \"center\""},
    {PROP_GAP, "gap", M_LAYOUT, VAL_INT, 0, 256, "gap = 16"},
    {PROP_WIDTH, "width", M_LAYOUT | M_IMAGE, VAL_PERCENT, 0, 100, "width = 50%"},
    {PROP_PADDING, "padding", M_LAYOUT, VAL_INT, 0, 256, "padding = 24"},
    {PROP_TO, "to", M_LINK | M_BUTTON, VAL_URL, 0, 0, "to = \"about.html\""},
    {PROP_ALT, "alt", M_IMAGE, VAL_STRING, 0, 0, "alt = \"A short description\""},
};

static const char *const PADDING_SIDES[PAD_COUNT] = {
    "", "block", "inline", "top", "bottom", "left", "right",
};

static const char *const ELEMENT_NAMES[] = {
    "column", "row", "heading", "text", "link", "button", "image",
};

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

static int token_equals(Token token, const char *text)
{
    size_t length = strlen(text);
    return token.length == length && memcmp(token.start, text, length) == 0;
}

static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int equals_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/* Levenshtein distance, capped for short identifiers. */
static size_t edit_distance(const char *a, size_t a_len, const char *b)
{
    size_t b_len = strlen(b);

    if (a_len > 32 || b_len > 32) {
        return 99;
    }

    size_t previous[33];
    size_t current[33];

    for (size_t j = 0; j <= b_len; j++) {
        previous[j] = j;
    }

    for (size_t i = 1; i <= a_len; i++) {
        current[0] = i;
        for (size_t j = 1; j <= b_len; j++) {
            size_t cost = ascii_lower((unsigned char)a[i - 1]) ==
                                  ascii_lower((unsigned char)b[j - 1])
                              ? 0
                              : 1;
            size_t best = previous[j] + 1;
            if (current[j - 1] + 1 < best) {
                best = current[j - 1] + 1;
            }
            if (previous[j - 1] + cost < best) {
                best = previous[j - 1] + cost;
            }
            current[j] = best;
        }
        memcpy(previous, current, sizeof(size_t) * (b_len + 1));
    }

    return previous[b_len];
}

/* Returns the closest candidate within an edit distance of 2, or NULL. */
static const char *suggest(Token token, const char *const *candidates,
                           size_t count)
{
    const char *best = NULL;
    size_t best_distance = 3;

    for (size_t i = 0; i < count; i++) {
        size_t distance = edit_distance(token.start, token.length, candidates[i]);
        if (distance < best_distance) {
            best = candidates[i];
            best_distance = distance;
        }
    }

    /* Avoid silly suggestions for very short words. */
    if (best != NULL && token.length <= 2 && best_distance > 1) {
        return NULL;
    }

    return best;
}

static char *copy_string(Parser *parser, const char *text, size_t length)
{
    char *copy = malloc(length + 1);

    if (copy == NULL) {
        parser->out_of_memory = 1;
        parser->failed = 1;
        return NULL;
    }

    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

/* Decodes a STRING token (including its quotes) into a new heap string. */
static char *decode_string(Parser *parser, Token token)
{
    const char *text = token.start + 1;
    size_t length = token.length >= 2 ? token.length - 2 : 0;
    char *out = malloc(length + 1);

    if (out == NULL) {
        parser->out_of_memory = 1;
        parser->failed = 1;
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < length; i++) {
        /* The lexer guarantees escapes are only \" and \\. */
        if (text[i] == '\\' && i + 1 < length) {
            i++;
        }
        out[j++] = text[i];
    }
    out[j] = '\0';
    return out;
}

/* ------------------------------------------------------------------------ */
/* Error reporting                                                          */
/* ------------------------------------------------------------------------ */

#define REPORT_AT(parser, token, hint, ...)                                    \
    diag_report((parser)->diags, DIAG_ERROR, (token).line, (token).column,    \
                (token).width, (hint), __VA_ARGS__)

#define WARN_AT(parser, token, hint, ...)                                      \
    diag_report((parser)->diags, DIAG_WARNING, (token).line, (token).column,  \
                (token).width, (hint), __VA_ARGS__)

/* Formats a token for use inside a message, e.g. 'foo' or "a number". */
static void describe_token(Token token, char *out, size_t out_size)
{
    switch (token.type) {
    case TOKEN_IDENTIFIER:
    case TOKEN_NUMBER:
    case TOKEN_PERCENTAGE:
        if (token.length <= 40) {
            snprintf(out, out_size, "'%.*s'", (int)token.length, token.start);
            return;
        }
        break;
    case TOKEN_STRING:
        if (token.length <= 40) {
            snprintf(out, out_size, "the text %.*s", (int)token.length, token.start);
            return;
        }
        break;
    default:
        break;
    }
    snprintf(out, out_size, "%s", token_type_describe(token.type));
}

static void report_lexer_error(Parser *parser, Token token)
{
    switch (token.error) {
    case LEX_UNTERMINATED_STRING:
        REPORT_AT(parser, token,
                  "Add a closing \" before the end of the line. Strings cannot span lines.",
                  "This string is missing its closing quote.");
        break;
    case LEX_INVALID_ESCAPE:
        REPORT_AT(parser, token,
                  "Inside quotes, use \\\" for a quote and \\\\ for a backslash.",
                  "Unknown escape sequence '%.*s'.", (int)token.length, token.start);
        break;
    case LEX_CONTROL_CHARACTER_IN_STRING:
        REPORT_AT(parser, token, "Remove the invisible character from the string.",
                  "Strings cannot contain control characters.");
        break;
    case LEX_CURLY_QUOTE:
        REPORT_AT(parser, token,
                  "Replace curly quotes with straight double quotes: \"Hello\". "
                  "Word processors and some chat apps convert them automatically.",
                  "Found a curly quote (%.*s). Genesis+ strings use straight quotes.",
                  (int)token.length, token.start);
        break;
    case LEX_NUMBER_WITH_UNIT:
        REPORT_AT(parser, token,
                  "Sizes are plain numbers measured in pixels, e.g. size = 16. "
                  "Widths are percentages, e.g. width = 50%.",
                  "Unexpected unit in '%.*s'.", (int)token.length, token.start);
        break;
    case LEX_MALFORMED_NUMBER:
        REPORT_AT(parser, token, "Write numbers like 16 or 33.5.",
                  "'%.*s' is not a valid number.", (int)token.length, token.start);
        break;
    case LEX_NEGATIVE_NUMBER:
        REPORT_AT(parser, token, NULL,
                  "Negative values like '%.*s' are not allowed.",
                  (int)token.length, token.start);
        break;
    case LEX_NUL_BYTE:
        REPORT_AT(parser, token,
                  "The file contains a NUL byte. Make sure it is saved as UTF-8 text.",
                  "Unexpected NUL character.");
        break;
    case LEX_UNEXPECTED_CHARACTER:
    case LEX_OK:
    default: {
        unsigned char first = (unsigned char)token.start[0];
        if (first >= 0x80) {
            REPORT_AT(parser, token,
                      "Text such as words and emoji must be inside double quotes.",
                      "Unexpected character '%.*s'.", (int)token.length, token.start);
        } else if (first < 0x20 || first == 0x7F) {
            REPORT_AT(parser, token, "Remove the invisible control character.",
                      "Unexpected control character (code %u).", (unsigned)first);
        } else if (first == '(' || first == ')' || first == '[' || first == ']') {
            REPORT_AT(parser, token, "Blocks use curly braces: column { ... }",
                      "Unexpected character '%c'.", first);
        } else if (first == '\'') {
            REPORT_AT(parser, token, "Use double quotes for text: \"Hello\".",
                      "Single quotes are not used in Genesis+.");
        } else {
            REPORT_AT(parser, token, NULL, "Unexpected character '%c'.", first);
        }
        break;
    }
    }
}

/* Reports a structural error and stops the parse. */
static void fail(Parser *parser)
{
    parser->failed = 1;
}

/* ------------------------------------------------------------------------ */
/* Token stream                                                             */
/* ------------------------------------------------------------------------ */

static void advance(Parser *parser)
{
    parser->current = parser->next;

    if (parser->current.type != TOKEN_EOF) {
        parser->next = lexer_next_token(&parser->lexer);
    }

    if (parser->current.type == TOKEN_ERROR && !parser->failed) {
        report_lexer_error(parser, parser->current);
        fail(parser);
    }
}

static int check(const Parser *parser, TokenType type)
{
    return parser->current.type == type;
}

static void report_legacy_colon(Parser *parser, const char *what)
{
    REPORT_AT(parser, parser->current,
              "Replace ':' with '{' and add a matching '}' after the last item "
              "inside the block. Indentation no longer matters.",
              "Genesis+ now uses braces instead of ':' and indentation to start a %s block.",
              what);
    fail(parser);
}

/* ------------------------------------------------------------------------ */
/* Value parsing and validation                                             */
/* ------------------------------------------------------------------------ */

/* Parses a NUMBER token as a whole number. Returns 0 on failure. */
static int parse_whole_number(Token token, int *out)
{
    if (token.type != TOKEN_NUMBER) {
        return 0;
    }

    long value = 0;

    for (size_t i = 0; i < token.length; i++) {
        char c = token.start[i];
        if (c < '0' || c > '9') {
            return 0;
        }
        if (value > 100000000L) {
            value = 100000001L; /* saturate: certainly out of range */
            continue;
        }
        value = value * 10 + (c - '0');
    }

    *out = value > 100000000L ? 100000001 : (int)value;
    return 1;
}

static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int is_valid_hex_color(const char *text)
{
    size_t length = strlen(text);

    if ((length != 4 && length != 7) || text[0] != '#') {
        return 0;
    }

    for (size_t i = 1; i < length; i++) {
        if (!is_hex_digit(text[i])) {
            return 0;
        }
    }

    return 1;
}

/*
 * Checks a link destination. Returns NULL when valid, otherwise a message.
 * Allowed: relative paths ("about.html", "#contact", "docs/"), and absolute
 * URLs using http, https, mailto or tel. Everything else (notably
 * "javascript:") is rejected.
 */
static const char *check_url(const char *url, char *scheme_out, size_t scheme_size)
{
    if (url[0] == '\0') {
        return "The destination is empty.";
    }

    for (const char *p = url; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7F) {
            return "The destination contains a control character.";
        }
        if (c == ' ') {
            return "The destination contains a space. Use %20 instead of spaces in links.";
        }
        if (c == '\\') {
            return "The destination contains a backslash. Use '/' to separate folders.";
        }
        if (c == '"' || c == '<' || c == '>' || c == '`') {
            return "The destination contains a character that is not allowed in links.";
        }
    }

    /* A scheme is the text before a ':' that appears before any '/', '?' or '#'. */
    size_t span = strcspn(url, ":/?#");

    if (url[span] != ':') {
        return NULL; /* relative URL */
    }

    if (span == 0) {
        return "The destination cannot start with ':'.";
    }

    snprintf(scheme_out, scheme_size, "%.*s", (int)(span < 30 ? span : 30), url);

    static const char *const allowed[] = {"http", "https", "mailto", "tel"};
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (equals_ignore_case(scheme_out, allowed[i])) {
            return NULL;
        }
    }

    return "unsupported-scheme";
}

static int starts_with_ignore_case(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (ascii_lower((unsigned char)*text) != ascii_lower((unsigned char)*prefix)) {
            return 0;
        }
        text++;
        prefix++;
    }
    return 1;
}

/* Validates an image source. Reports errors itself. */
static void validate_image_source(Parser *parser, Token token, const char *src)
{
    if (starts_with_ignore_case(src, "http://") || starts_with_ignore_case(src, "https://")) {
        char scheme[32];
        const char *problem = check_url(src, scheme, sizeof(scheme));
        if (problem != NULL) {
            REPORT_AT(parser, token, NULL, "Invalid image address. %s", problem);
        }
        return;
    }

    if (src[0] == '\0') {
        REPORT_AT(parser, token, "Example: image \"assets/photo.jpg\"",
                  "The image source is empty.");
        return;
    }

    for (const char *p = src; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7F || c == '"' || c == '<' || c == '>' || c == '`' ||
            c == ':' || c == '\\') {
            REPORT_AT(parser, token,
                      "Local images are written like \"assets/photo.jpg\", using '/' between folders.",
                      "The image path contains a character that is not allowed ('%c').",
                      c >= 0x20 && c != 0x7F ? c : '?');
            return;
        }
    }

    if (!starts_with_ignore_case(src, "assets/") || src[7] == '\0') {
        REPORT_AT(parser, token,
                  "Put image files in a folder called 'assets' next to your .gns file, "
                  "then write: image \"assets/photo.jpg\". Web addresses starting with "
                  "https:// also work.",
                  "Local images must be inside the assets folder.");
        return;
    }

    /* Reject any ".." path segment so images cannot point outside the project. */
    const char *segment = src;
    while (*segment != '\0') {
        size_t len = strcspn(segment, "/");
        if ((len == 2 && segment[0] == '.' && segment[1] == '.') || len == 0) {
            REPORT_AT(parser, token, "Remove '..' and empty folders ('//') from the path.",
                      "The image path must stay inside the assets folder.");
            return;
        }
        segment += len;
        if (*segment == '/') {
            segment++;
            if (*segment == '\0') {
                REPORT_AT(parser, token, "Example: image \"assets/photo.jpg\"",
                          "The image path ends with '/' but should name a file.");
                return;
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Properties                                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    /* Line where each property (and padding side) was first set, or 0. */
    size_t seen_line[PROP_COUNT][PAD_COUNT];
    size_t seen_column[PROP_COUNT][PAD_COUNT];
} PropertyState;

static const PropertySpec *find_property(Token name)
{
    for (size_t i = 0; i < PROP_COUNT; i++) {
        if (token_equals(name, PROPERTIES[i].name)) {
            return &PROPERTIES[i];
        }
    }
    return NULL;
}

/* Writes the names of properties valid for `type`, comma separated. */
static void list_properties_for(AstNodeType type, char *out, size_t out_size)
{
    out[0] = '\0';
    size_t used = 0;

    for (size_t i = 0; i < PROP_COUNT; i++) {
        if (PROPERTIES[i].elements & MASK(type)) {
            int written = snprintf(out + used, out_size - used, "%s%s",
                                   used == 0 ? "" : ", ", PROPERTIES[i].name);
            if (written < 0 || (size_t)written >= out_size - used) {
                return;
            }
            used += (size_t)written;
        }
    }

    if (used == 0) {
        snprintf(out, out_size, "(none)");
    }
}

static void list_elements_for(const PropertySpec *spec, char *out, size_t out_size)
{
    static const AstNodeType order[] = {
        AST_PAGE, AST_COLUMN, AST_ROW, AST_HEADING, AST_TEXT,
        AST_LINK, AST_BUTTON, AST_IMAGE,
    };

    out[0] = '\0';
    size_t used = 0;

    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (spec->elements & MASK(order[i])) {
            int written = snprintf(out + used, out_size - used, "%s%s",
                                   used == 0 ? "" : ", ", ast_type_name(order[i]));
            if (written < 0 || (size_t)written >= out_size - used) {
                return;
            }
            used += (size_t)written;
        }
    }
}

static void report_unknown_property(Parser *parser, AstNode *node, Token name)
{
    const char *candidates[PROP_COUNT];
    size_t count = 0;

    for (size_t i = 0; i < PROP_COUNT; i++) {
        if (PROPERTIES[i].elements & MASK(node->type)) {
            candidates[count++] = PROPERTIES[i].name;
        }
    }

    char available[160];
    list_properties_for(node->type, available, sizeof(available));

    char hint[DIAG_HINT_MAX];
    const char *guess = suggest(name, candidates, count);

    /* Common CSS spellings get a direct pointer to the Genesis+ name. */
    if (guess == NULL) {
        if (token_equals(name, "colour") || token_equals(name, "fontcolor")) {
            guess = "color";
        } else if (token_equals(name, "fontsize") || token_equals(name, "font")) {
            guess = "size";
        } else if (token_equals(name, "href") || token_equals(name, "url") ||
                   token_equals(name, "link")) {
            guess = "to";
        }
    }

    /* Only suggest names that are valid for this element. */
    if (guess != NULL) {
        int valid = 0;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(candidates[i], guess) == 0) {
                valid = 1;
            }
        }
        if (!valid) {
            guess = NULL;
        }
    }

    if (guess != NULL) {
        snprintf(hint, sizeof(hint), "Did you mean '%s'? Properties for %s: %s.",
                 guess, ast_type_name(node->type), available);
    } else {
        snprintf(hint, sizeof(hint), "Properties for %s: %s.",
                 ast_type_name(node->type), available);
    }

    REPORT_AT(parser, name, hint, "Unknown property '%.*s' on %s.",
              (int)name.length, name.start, ast_type_name(node->type));
}

static void apply_color(Parser *parser, AstColor *color, Token value_token,
                        const char *decoded, const char *what)
{
    if (!is_valid_hex_color(decoded)) {
        REPORT_AT(parser, value_token,
                  "Use a hex color such as \"#7c3aed\" or the short form \"#fff\". "
                  "Color names like \"red\" are not supported.",
                  "Invalid %s %.*s. Expected #RGB or #RRGGBB.", what,
                  (int)(value_token.length <= 40 ? value_token.length : 40),
                  value_token.start);
        return;
    }

    color->set = 1;
    snprintf(color->value, sizeof(color->value), "%s", decoded);
}

static void parse_percentage(Parser *parser, AstNode *node, Token token)
{
    /* token text is like "50%", "33.33%" */
    long whole = 0;
    long fraction = 0;
    int fraction_digits = 0;
    int in_fraction = 0;
    int too_large = 0;

    for (size_t i = 0; i + 1 < token.length; i++) {
        char c = token.start[i];
        if (c == '.') {
            in_fraction = 1;
            continue;
        }
        if (in_fraction) {
            fraction_digits++;
            if (fraction_digits <= 2) {
                fraction = fraction * 10 + (c - '0');
            }
        } else if (whole > 100000) {
            too_large = 1;
        } else {
            whole = whole * 10 + (c - '0');
        }
    }

    if (fraction_digits > 2) {
        REPORT_AT(parser, token, "For example: width = 33.33%",
                  "Width '%.*s' has too many decimal places; use at most two.",
                  (int)token.length, token.start);
        return;
    }

    if (fraction_digits == 1) {
        fraction *= 10;
    }

    long hundredths = whole * 100 + fraction;

    if (too_large || hundredths <= 0 || hundredths > 10000) {
        REPORT_AT(parser, token,
                  "Width is a share of the available space, e.g. width = 50%.",
                  "Invalid width '%.*s'. Width must be more than 0%% and at most 100%%.",
                  (int)token.length, token.start);
        return;
    }

    node->has_width = 1;
    node->width_hundredths = (int)hundredths;
}

static void apply_property(Parser *parser, AstNode *node, const PropertySpec *spec,
                           int side, Token value)
{
    char found[64];

    /* First check that the value has the right basic type. */
    switch (spec->kind) {
    case VAL_INT:
        if (value.type != TOKEN_NUMBER) {
            describe_token(value, found, sizeof(found));
            REPORT_AT(parser, value, NULL,
                      "'%s' needs a whole number, like %s, but found %s.",
                      spec->name, spec->example, found);
            return;
        }
        break;
    case VAL_PERCENT:
        if (value.type != TOKEN_PERCENTAGE) {
            describe_token(value, found, sizeof(found));
            REPORT_AT(parser, value, "Widths are percentages of the available space.",
                      "'%s' needs a percentage, like %s, but found %s.",
                      spec->name, spec->example, found);
            return;
        }
        break;
    case VAL_COLOR:
    case VAL_ALIGN:
    case VAL_STRING:
    case VAL_URL:
        if (value.type != TOKEN_STRING) {
            describe_token(value, found, sizeof(found));
            REPORT_AT(parser, value, "Text values are written in double quotes.",
                      "'%s' needs a value in quotes, like %s, but found %s.",
                      spec->name, spec->example, found);
            return;
        }
        break;
    }

    if (spec->kind == VAL_INT) {
        int number = 0;
        if (!parse_whole_number(value, &number)) {
            REPORT_AT(parser, value, NULL,
                      "'%s' needs a whole number, like %s, but found '%.*s'.",
                      spec->name, spec->example, (int)value.length, value.start);
            return;
        }

        if (number < spec->min || number > spec->max) {
            REPORT_AT(parser, value, NULL,
                      "Invalid %s %.*s. '%s' must be between %d and %d%s.",
                      spec->name, (int)(value.length <= 12 ? value.length : 12),
                      value.start, spec->name, spec->min, spec->max,
                      spec->id == PROP_LEVEL ? "" : " (pixels)");
            return;
        }

        switch (spec->id) {
        case PROP_SIZE:
            node->size = number;
            break;
        case PROP_LEVEL:
            node->level = number;
            break;
        case PROP_GAP:
            node->gap = number;
            node->has_gap = 1;
            break;
        case PROP_PADDING:
            node->padding[side] = number;
            node->has_padding[side] = 1;
            break;
        default:
            break;
        }
        return;
    }

    if (spec->kind == VAL_PERCENT) {
        parse_percentage(parser, node, value);
        return;
    }

    char *text = decode_string(parser, value);
    if (text == NULL) {
        return;
    }

    switch (spec->id) {
    case PROP_BACKGROUND:
        apply_color(parser, &node->background, value, text, "background color");
        break;

    case PROP_COLOR:
        apply_color(parser, &node->color, value, text, "color");
        break;

    case PROP_ALIGN:
        if (strcmp(text, "left") == 0) {
            node->align = ALIGN_LEFT;
        } else if (strcmp(text, "center") == 0) {
            node->align = ALIGN_CENTER;
        } else if (strcmp(text, "right") == 0) {
            node->align = ALIGN_RIGHT;
        } else {
            REPORT_AT(parser, value, "Use \"left\", \"center\", or \"right\".",
                      "Invalid align value %.*s.",
                      (int)(value.length <= 40 ? value.length : 40), value.start);
        }
        break;

    case PROP_TITLE:
        if (text[0] == '\0') {
            REPORT_AT(parser, value, "Example: title = \"My website\"",
                      "The page title cannot be empty.");
            break;
        }
        free(node->title);
        node->title = text;
        text = NULL;
        break;

    case PROP_ALT:
        free(node->alt);
        node->alt = text;
        text = NULL;
        break;

    case PROP_TO: {
        char scheme[32] = "";
        const char *problem = check_url(text, scheme, sizeof(scheme));
        if (problem != NULL && strcmp(problem, "unsupported-scheme") == 0) {
            REPORT_AT(parser, value,
                      "Use a page in your site like \"about.html\", a section like "
                      "\"#contact\", or an address starting with https://, http://, "
                      "mailto: or tel:.",
                      "Links starting with '%s:' are not allowed.", scheme);
        } else if (problem != NULL) {
            REPORT_AT(parser, value, NULL, "Invalid link destination. %s", problem);
        } else {
            free(node->href);
            node->href = text;
            text = NULL;
        }
        break;
    }

    default:
        break;
    }

    free(text);
}

/*
 * Parses `name [side] = value` properties until the next token is not a
 * property. Each property is applied to `node` and validated.
 */
static void parse_properties(Parser *parser, AstNode *node)
{
    PropertyState state;
    memset(&state, 0, sizeof(state));
    parser->properties_seen = 0;

    while (!parser->failed && check(parser, TOKEN_IDENTIFIER)) {
        Token name = parser->current;
        int is_padding = token_equals(name, "padding");

        /* An identifier not followed by '=' (or a padding side) is not a
           property; let the caller decide what it is. */
        if (parser->next.type != TOKEN_EQUAL &&
            !(is_padding && parser->next.type == TOKEN_IDENTIFIER)) {
            return;
        }

        advance(parser);

        int side = PAD_ALL;
        int side_ok = 1;
        Token side_token = name;

        if (is_padding && check(parser, TOKEN_IDENTIFIER)) {
            side_token = parser->current;
            side = -1;
            for (int i = 1; i < PAD_COUNT; i++) {
                if (token_equals(side_token, PADDING_SIDES[i])) {
                    side = i;
                }
            }

            if (side < 0) {
                const char *guess = suggest(side_token, PADDING_SIDES + 1, PAD_COUNT - 1);
                char hint[DIAG_HINT_MAX];
                snprintf(hint, sizeof(hint),
                         "%s%s%sPadding sides are: top, bottom, left, right, inline "
                         "(left and right), and block (top and bottom).",
                         guess ? "Did you mean '" : "", guess ? guess : "",
                         guess ? "'? " : "");
                REPORT_AT(parser, side_token, hint, "Unknown padding side '%.*s'.",
                          (int)side_token.length, side_token.start);
                side_ok = 0;
                side = PAD_ALL;
            }

            advance(parser);
        }

        if (parser->failed) {
            return;
        }

        if (!check(parser, TOKEN_EQUAL)) {
            char found[64];
            describe_token(parser->current, found, sizeof(found));
            REPORT_AT(parser, parser->current, "Example: padding top = 12",
                      "Expected '=' after 'padding %.*s', but found %s.",
                      (int)side_token.length, side_token.start, found);
            fail(parser);
            return;
        }

        Token equal = parser->current;
        advance(parser);

        if (parser->failed) {
            return;
        }

        Token value = parser->current;

        if (value.type == TOKEN_IDENTIFIER) {
            /* Recoverable: the user probably forgot quotes. */
            const PropertySpec *spec = find_property(name);
            char hint[DIAG_HINT_MAX];
            snprintf(hint, sizeof(hint), "Example: %s",
                     spec != NULL ? spec->example : "align = \"center\"");
            REPORT_AT(parser, value, hint,
                      "The value '%.*s' must be in double quotes (or be a number).",
                      (int)value.length, value.start);
            advance(parser);
            continue;
        }

        if (value.type != TOKEN_STRING && value.type != TOKEN_NUMBER &&
            value.type != TOKEN_PERCENTAGE) {
            const PropertySpec *spec = find_property(name);
            char found[64];
            char hint[DIAG_HINT_MAX];
            describe_token(value, found, sizeof(found));
            snprintf(hint, sizeof(hint), "Example: %s",
                     spec != NULL ? spec->example : "size = 24");
            diag_report(parser->diags, DIAG_ERROR, equal.line, equal.column, 1, hint,
                        "Expected a value after '%.*s =', but found %s.",
                        (int)name.length, name.start, found);
            fail(parser);
            return;
        }

        advance(parser);

        const PropertySpec *spec = find_property(name);

        if (spec == NULL) {
            report_unknown_property(parser, node, name);
            continue;
        }

        if (!(spec->elements & MASK(node->type))) {
            char elements[128];
            char hint[DIAG_HINT_MAX];
            list_elements_for(spec, elements, sizeof(elements));
            snprintf(hint, sizeof(hint), "'%s' works on: %s.", spec->name, elements);

            if (spec->id == PROP_PADDING || spec->id == PROP_GAP) {
                snprintf(hint, sizeof(hint),
                         "'%s' works on: %s. To space out a %s, place it inside a "
                         "column and give the column %s.",
                         spec->name, elements, ast_type_name(node->type), spec->name);
            }

            REPORT_AT(parser, name, hint, "'%s' cannot be used on %s.", spec->name,
                      ast_type_name(node->type));
            continue;
        }

        if (!side_ok) {
            continue;
        }

        if (state.seen_line[spec->id][side] != 0) {
            char hint[DIAG_HINT_MAX];
            snprintf(hint, sizeof(hint),
                     "It was already set at line %zu, column %zu. Remove one of them.",
                     state.seen_line[spec->id][side], state.seen_column[spec->id][side]);
            if (side == PAD_ALL) {
                REPORT_AT(parser, name, hint, "Duplicate property '%s'.", spec->name);
            } else {
                REPORT_AT(parser, name, hint, "Duplicate property 'padding %s'.",
                          PADDING_SIDES[side]);
            }
            continue;
        }

        state.seen_line[spec->id][side] = name.line;
        parser->properties_seen |= 1u << spec->id;
        state.seen_column[spec->id][side] = name.column;

        apply_property(parser, node, spec, side, value);
    }
}

/* ------------------------------------------------------------------------ */
/* Statements and blocks                                                    */
/* ------------------------------------------------------------------------ */

static void parse_block(Parser *parser, AstNode *node);

static AstNode *new_child(Parser *parser, AstNode *parent, AstNodeType type, Token at)
{
    AstNode *node = ast_create_node(type, at.line, at.column);

    if (node == NULL) {
        parser->out_of_memory = 1;
        fail(parser);
        return NULL;
    }

    ast_add_child(parent, node);
    return node;
}

static void parse_layout(Parser *parser, AstNode *parent)
{
    Token keyword = parser->current;
    AstNodeType type = keyword.type == TOKEN_ROW ? AST_ROW : AST_COLUMN;
    AstNode *node = new_child(parser, parent, type, keyword);

    if (node == NULL) {
        return;
    }

    advance(parser);
    parse_properties(parser, node);

    if (!parser->failed) {
        parse_block(parser, node);
    }
}

static void parse_content(Parser *parser, AstNode *parent)
{
    Token keyword = parser->current;
    AstNodeType type;

    switch (keyword.type) {
    case TOKEN_HEADING: type = AST_HEADING; break;
    case TOKEN_TEXT:    type = AST_TEXT; break;
    case TOKEN_LINK:    type = AST_LINK; break;
    case TOKEN_BUTTON:  type = AST_BUTTON; break;
    default:            type = AST_IMAGE; break;
    }

    const char *name = ast_type_name(type);
    const char *example;

    switch (type) {
    case AST_HEADING: example = "Example: heading \"Welcome\""; break;
    case AST_TEXT:    example = "Example: text \"Hello there\""; break;
    case AST_LINK:    example = "Example: link \"About us\" to = \"about.html\""; break;
    case AST_BUTTON:  example = "Example: button \"Get started\" to = \"#start\""; break;
    default:          example = "Example: image \"assets/photo.jpg\" alt = \"A photo\""; break;
    }

    AstNode *node = new_child(parser, parent, type, keyword);
    if (node == NULL) {
        return;
    }

    advance(parser);
    if (parser->failed) {
        return;
    }

    if (!check(parser, TOKEN_STRING)) {
        char found[64];
        describe_token(parser->current, found, sizeof(found));

        if (check(parser, TOKEN_LBRACE)) {
            REPORT_AT(parser, parser->current, example,
                      "'%s' needs its %s in quotes right after it, and it cannot "
                      "contain other elements.",
                      name, type == AST_IMAGE ? "file path" : "text");
        } else if (check(parser, TOKEN_IDENTIFIER)) {
            REPORT_AT(parser, parser->current, example,
                      "Expected %s in double quotes after '%s', but found %s.",
                      type == AST_IMAGE ? "an image path" : "text", name, found);
        } else {
            REPORT_AT(parser, parser->current, example,
                      "Expected %s in double quotes after '%s', but found %s.",
                      type == AST_IMAGE ? "an image path" : "text", name, found);
        }
        fail(parser);
        return;
    }

    Token value = parser->current;
    node->value = decode_string(parser, value);
    if (node->value == NULL) {
        return;
    }

    advance(parser);
    parse_properties(parser, node);

    if (parser->failed) {
        return;
    }

    if (check(parser, TOKEN_LBRACE)) {
        REPORT_AT(parser, parser->current,
                  "Only page, column and row can contain other elements. "
                  "Remove the braces, or wrap the content in a column.",
                  "A %s cannot contain other elements.", name);
        fail(parser);
        return;
    }

    if (check(parser, TOKEN_COLON)) {
        REPORT_AT(parser, parser->current, "Remove the ':'.",
                  "Unexpected ':' after %s.", name);
        fail(parser);
        return;
    }

    /* Semantic checks that need the complete element. */
    if (type == AST_IMAGE) {
        validate_image_source(parser, value, node->value);

        if (node->alt == NULL) {
            WARN_AT(parser, keyword,
                    "Describe the image for people using screen readers: "
                    "image \"assets/photo.jpg\" alt = \"A sunset over the sea\"",
                    "This image has no 'alt' description.");
        }
    } else if (node->value[0] == '\0') {
        WARN_AT(parser, value, NULL, "This %s is empty.", name);
    }

    /* Only when `to` is missing entirely; an invalid `to` was already reported. */
    if ((type == AST_LINK || type == AST_BUTTON) &&
        !(parser->properties_seen & (1u << PROP_TO))) {
        REPORT_AT(parser, keyword, example, "This %s needs a destination: add to = \"...\".",
                  name);
    }

    if (type == AST_HEADING && node->level == 0) {
        node->level = 1;
    }
}

static void report_unknown_statement(Parser *parser, AstNode *parent)
{
    Token token = parser->current;

    if (token.type == TOKEN_IDENTIFIER && parser->next.type == TOKEN_EQUAL) {
        char hint[DIAG_HINT_MAX];
        snprintf(hint, sizeof(hint),
                 "Properties go right after the element they style, before its '{'. "
                 "For example: %s %.*s = ... {",
                 parent->type == AST_PAGE ? "page Home" : ast_type_name(parent->type),
                 (int)token.length, token.start);
        REPORT_AT(parser, token, hint, "The property '%.*s' is in the wrong place.",
                  (int)token.length, token.start);
        return;
    }

    const PropertySpec *property = token.type == TOKEN_IDENTIFIER ? find_property(token) : NULL;

    if (property != NULL) {
        char hint[DIAG_HINT_MAX];
        snprintf(hint, sizeof(hint), "Properties are written as name = value, e.g. %s",
                 property->example);
        REPORT_AT(parser, token, hint, "Expected '=' after '%s'.", property->name);
        return;
    }

    if (token.type == TOKEN_IDENTIFIER) {
        const char *guess = suggest(token, ELEMENT_NAMES,
                                    sizeof(ELEMENT_NAMES) / sizeof(ELEMENT_NAMES[0]));
        char hint[DIAG_HINT_MAX];

        if (guess == NULL) {
            if (token_equals(token, "p") || token_equals(token, "paragraph") ||
                token_equals(token, "span") || token_equals(token, "label")) {
                guess = "text";
            } else if (token_equals(token, "h1") || token_equals(token, "h2") ||
                       token_equals(token, "h3") || token_equals(token, "title")) {
                guess = "heading";
            } else if (token_equals(token, "a")) {
                guess = "link";
            } else if (token_equals(token, "img") || token_equals(token, "picture")) {
                guess = "image";
            } else if (token_equals(token, "div") || token_equals(token, "section") ||
                       token_equals(token, "stack")) {
                guess = "column";
            }
        }

        if (guess != NULL) {
            snprintf(hint, sizeof(hint),
                     "Did you mean '%s'? Elements are: column, row, heading, text, "
                     "link, button, image.",
                     guess);
        } else {
            snprintf(hint, sizeof(hint),
                     "Elements are: column, row, heading, text, link, button, image.");
        }

        REPORT_AT(parser, token, hint, "Unknown element '%.*s'.",
                  (int)token.length, token.start);
        return;
    }

    if (token.type == TOKEN_STRING) {
        REPORT_AT(parser, token,
                  "Text needs an element in front of it, e.g. text \"Hello\" or heading \"Hello\".",
                  "Unexpected text %.*s.", (int)(token.length <= 40 ? token.length : 40),
                  token.start);
        return;
    }

    if (token.type == TOKEN_LBRACE) {
        REPORT_AT(parser, token,
                  "A '{' must follow page, column or row, e.g. column { ... }.",
                  "Unexpected '{'.");
        return;
    }

    char found[64];
    describe_token(token, found, sizeof(found));
    REPORT_AT(parser, token,
              "Each item starts with an element name: column, row, heading, text, "
              "link, button or image.",
              "Unexpected %s.", found);
}

static void parse_statement(Parser *parser, AstNode *parent)
{
    switch (parser->current.type) {
    case TOKEN_SEMICOLON:
        advance(parser);
        return;

    case TOKEN_COLUMN:
    case TOKEN_ROW:
        parse_layout(parser, parent);
        return;

    case TOKEN_HEADING:
    case TOKEN_TEXT:
    case TOKEN_LINK:
    case TOKEN_BUTTON:
    case TOKEN_IMAGE:
        parse_content(parser, parent);
        return;

    case TOKEN_PAGE: {
        char hint[DIAG_HINT_MAX];
        snprintf(hint, sizeof(hint),
                 "Pages cannot be placed inside other blocks. Did you forget a '}' "
                 "to close the %s that starts at line %zu?",
                 ast_type_name(parent->type), parent->line);
        REPORT_AT(parser, parser->current, hint, "Unexpected 'page' inside a %s.",
                  ast_type_name(parent->type));
        fail(parser);
        return;
    }

    case TOKEN_COLON:
        REPORT_AT(parser, parser->current,
                  "Genesis+ uses braces: write column { ... } instead of column:",
                  "Unexpected ':'.");
        fail(parser);
        return;

    default:
        report_unknown_statement(parser, parent);
        fail(parser);
        return;
    }
}

static void parse_block(Parser *parser, AstNode *node)
{
    const char *name = ast_type_name(node->type);

    if (check(parser, TOKEN_COLON)) {
        report_legacy_colon(parser, name);
        return;
    }

    if (!check(parser, TOKEN_LBRACE)) {
        char found[64];
        char hint[DIAG_HINT_MAX];
        describe_token(parser->current, found, sizeof(found));

        if (check(parser, TOKEN_STRING)) {
            snprintf(hint, sizeof(hint),
                     "A %s holds other elements. Put text inside it: %s { text %.*s }",
                     name, node->type == AST_PAGE ? "page Home" : name,
                     (int)(parser->current.length <= 40 ? parser->current.length : 40),
                     parser->current.start);
        } else if (check(parser, TOKEN_IDENTIFIER) && node->type != AST_PAGE) {
            snprintf(hint, sizeof(hint),
                     "Properties are written as name = value, e.g. %s gap = 16 { ... }",
                     name);
        } else {
            snprintf(hint, sizeof(hint), "Example: %s { ... }",
                     node->type == AST_PAGE ? "page Home" : name);
        }

        REPORT_AT(parser, parser->current, hint,
                  "Expected '{' to start the %s's content, but found %s.", name, found);
        fail(parser);
        return;
    }

    Token open = parser->current;

    if (parser->depth >= PARSER_MAX_DEPTH) {
        REPORT_AT(parser, open, "Simplify the layout by removing some nested blocks.",
                  "Blocks are nested too deeply (the limit is %d levels).",
                  PARSER_MAX_DEPTH);
        fail(parser);
        return;
    }

    parser->depth++;
    advance(parser);

    while (!parser->failed) {
        if (check(parser, TOKEN_RBRACE)) {
            advance(parser);
            break;
        }

        if (check(parser, TOKEN_EOF)) {
            char hint[DIAG_HINT_MAX];
            snprintf(hint, sizeof(hint),
                     "Add '}' after the last item inside this %s. Every '{' needs a "
                     "matching '}'.",
                     name);
            REPORT_AT(parser, open, hint,
                      "The %s block that starts here is never closed: expected '}' "
                      "before the end of the file.",
                      name);
            fail(parser);
            break;
        }

        parse_statement(parser, node);
    }

    parser->depth--;

    if (!parser->failed && node->first_child == NULL) {
        WARN_AT(parser, open, NULL, "This %s is empty.", name);
    }
}

/* ------------------------------------------------------------------------ */
/* Pages and program                                                        */
/* ------------------------------------------------------------------------ */

static int is_reserved_file_name(const char *lower)
{
    static const char *const reserved[] = {
        "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
    };

    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (strcmp(lower, reserved[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void parse_page(Parser *parser, AstNode *program)
{
    Token keyword = parser->current;
    advance(parser);

    if (parser->failed) {
        return;
    }

    Token name = parser->current;

    if (name.type != TOKEN_IDENTIFIER) {
        if (token_is_keyword(name.type)) {
            REPORT_AT(parser, name, "Choose another name, e.g. page Home { ... }",
                      "'%.*s' is a Genesis+ keyword and cannot be used as a page name.",
                      (int)name.length, name.start);
        } else if (name.type == TOKEN_STRING) {
            REPORT_AT(parser, name, "Example: page Home { ... }",
                      "Page names are written without quotes.");
        } else {
            char found[64];
            describe_token(name, found, sizeof(found));
            REPORT_AT(parser, name,
                      "Every page needs a name made of letters, digits or '_', "
                      "e.g. page Home { ... }",
                      "Expected a page name after 'page', but found %s.", found);
        }
        fail(parser);
        return;
    }

    if (name.length > 64) {
        REPORT_AT(parser, name, NULL, "Page names can be at most 64 characters long.");
        fail(parser);
        return;
    }

    AstNode *page = new_child(parser, program, AST_PAGE, keyword);
    if (page == NULL) {
        return;
    }

    page->value = copy_string(parser, name.start, name.length);
    if (page->value == NULL) {
        return;
    }

    /* Remember where the name is for later diagnostics about it. */
    page->line = name.line;
    page->column = name.column;

    advance(parser);
    parse_properties(parser, page);

    if (!parser->failed) {
        parse_block(parser, page);
    }
}

/* Assigns output file names and checks page names for conflicts. */
static void assign_file_names(Parser *parser, AstNode *program)
{
    int index = 0;

    for (AstNode *page = program->first_child; page != NULL; page = page->next_sibling) {
        size_t length = strlen(page->value);
        char lower[80];

        for (size_t i = 0; i <= length && i < sizeof(lower) - 1; i++) {
            lower[i] = (char)ascii_lower((unsigned char)page->value[i]);
        }
        lower[length < sizeof(lower) - 1 ? length : sizeof(lower) - 1] = '\0';

        Token at;
        memset(&at, 0, sizeof(at));
        at.line = page->line;
        at.column = page->column;
        at.width = length;

        for (AstNode *other = program->first_child; other != page;
             other = other->next_sibling) {
            if (equals_ignore_case(other->value, page->value)) {
                char hint[DIAG_HINT_MAX];
                snprintf(hint, sizeof(hint),
                         "A page called '%s' already exists at line %zu. Page names must "
                         "be unique (ignoring upper/lower case).",
                         other->value, other->line);
                REPORT_AT(parser, at, hint, "Duplicate page name '%s'.", page->value);
                break;
            }
        }

        if (is_reserved_file_name(lower)) {
            REPORT_AT(parser, at, "Choose a different page name.",
                      "'%s' cannot be used as a page name because it is reserved by Windows.",
                      page->value);
        }

        char file_name[96];
        if (index == 0) {
            snprintf(file_name, sizeof(file_name), "index.html");
        } else {
            if (strcmp(lower, "index") == 0) {
                REPORT_AT(parser, at,
                          "The first page in the file becomes index.html. Rename this "
                          "page, or move it to the top of the file.",
                          "Only the first page can be called '%s'.", page->value);
            }
            snprintf(file_name, sizeof(file_name), "%s.html", lower);
        }

        page->file_name = copy_string(parser, file_name, strlen(file_name));
        if (page->file_name == NULL) {
            return;
        }

        index++;
    }
}

AstNode *parse_program(const char *source, size_t length, DiagList *diags)
{
    Parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.diags = diags;

    AstNode *program = ast_create_node(AST_PROGRAM, 1, 1);
    if (program == NULL) {
        diag_report(diags, DIAG_ERROR, 1, 1, 1, NULL, "Out of memory.");
        return NULL;
    }

    lexer_init(&parser.lexer, source, length);
    parser.next = lexer_next_token(&parser.lexer);
    advance(&parser);

    while (!parser.failed && !check(&parser, TOKEN_EOF)) {
        switch (parser.current.type) {
        case TOKEN_PAGE:
            parse_page(&parser, program);
            break;

        case TOKEN_SEMICOLON:
            advance(&parser);
            break;

        case TOKEN_RBRACE:
            REPORT_AT(&parser, parser.current,
                      "Remove this '}', or check whether a block has one '}' too many.",
                      "Unexpected '}': there is no open block to close here.");
            fail(&parser);
            break;

        case TOKEN_COLUMN:
        case TOKEN_ROW:
        case TOKEN_HEADING:
        case TOKEN_TEXT:
        case TOKEN_LINK:
        case TOKEN_BUTTON:
        case TOKEN_IMAGE:
            REPORT_AT(&parser, parser.current,
                      program->first_child == NULL
                          ? "Wrap your content in a page: page Home { ... }"
                          : "Did a page end too early? Check for an extra '}' above.",
                      "'%.*s' must be inside a page.", (int)parser.current.length,
                      parser.current.start);
            fail(&parser);
            break;

        default: {
            char found[64];
            describe_token(parser.current, found, sizeof(found));
            REPORT_AT(&parser, parser.current,
                      "A Genesis+ file is made of pages, e.g. page Home { heading \"Hello\" }",
                      "Expected 'page', but found %s.", found);
            fail(&parser);
            break;
        }
        }
    }

    if (parser.out_of_memory) {
        diag_report(diags, DIAG_ERROR, parser.current.line, parser.current.column, 1, NULL,
                    "Out of memory.");
        return program;
    }

    if (!parser.failed && program->first_child == NULL) {
        diag_report(diags, DIAG_ERROR, parser.current.line, parser.current.column, 1,
                    "Start with a page, for example:  page Home { heading \"Hello\" }",
                    "This file has no pages.");
        return program;
    }

    if (!parser.failed) {
        assign_file_names(&parser, program);
    }

    return program;
}
