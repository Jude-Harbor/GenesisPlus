"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");

const {
    parseMachineDiagnostics,
    codePointColumnToOffset,
    codePointLengthToUtf16,
    tokenize,
    completionContext,
} = require("../lib/analysis");

test("parses machine diagnostics", () => {
    const parsed = parseMachineDiagnostics(
        "error\t3\t12\t4\tInvalid width '120%'. Hint: use 50%\r\n" +
            "warning\t1\t1\t1\tThis column is empty.\n" +
            "garbage line\n\n"
    );
    assert.deepEqual(parsed, [
        { severity: "error", line: 3, column: 12, length: 4, message: "Invalid width '120%'. Hint: use 50%" },
        { severity: "warning", line: 1, column: 1, length: 1, message: "This column is empty." },
    ]);
});

test("converts code point columns to UTF-16 offsets", () => {
    assert.equal(codePointColumnToOffset("abc", 1), 0);
    assert.equal(codePointColumnToOffset("abc", 3), 2);
    /* The rocket emoji is one code point but two UTF-16 units. */
    assert.equal(codePointColumnToOffset('"\u{1F680}" x', 4), 4);
    assert.equal(codePointColumnToOffset("ab", 5), 4);
    assert.equal(codePointLengthToUtf16("\u{1F680}\u{1F680}", 0, 2), 4);
    assert.equal(codePointLengthToUtf16("abc", 3, 1), 1);
});

test("tokenizer handles strings, comments and braces", () => {
    const tokens = tokenize('page A{text "a \\" # b" # c\n}');
    assert.deepEqual(
        tokens.map((t) => t.type),
        ["keyword", "word", "{", "keyword", "string", "comment", "}"]
    );
    assert.equal(tokens[4].closed, true);
});

test("completion: statements", () => {
    assert.deepEqual(completionContext(""), { kind: "statement", topLevel: true });
    assert.deepEqual(completionContext("page Home {\n    "), { kind: "statement", topLevel: false });
    assert.deepEqual(completionContext("page Home {\n    col"), { kind: "statement", topLevel: false });
    assert.deepEqual(completionContext("page Home {\n    column {\n    }\n    "),
        { kind: "statement", topLevel: false });
    assert.deepEqual(completionContext("page Home { }\n"), { kind: "statement", topLevel: true });
});

test("completion: properties for the current element", () => {
    const heading = completionContext('page A {\n    heading "Hi" size = 24 ');
    assert.equal(heading.kind, "properties");
    assert.equal(heading.element, "heading");
    assert.deepEqual(heading.used, ["size"]);
    assert.equal(heading.alsoStatement, true);

    const column = completionContext("page A {\n    column gap = 4 padding top = 2 ");
    assert.equal(column.kind, "properties");
    assert.equal(column.element, "column");
    assert.deepEqual(column.used, ["gap", "padding top"]);
    assert.equal(column.alsoStatement, false);

    const page = completionContext("page Home ");
    assert.equal(page.kind, "properties");
    assert.equal(page.element, "page");
});

test("completion: values, padding sides and nothing inside strings", () => {
    assert.deepEqual(completionContext('page A { text "x" align = '),
        { kind: "value", element: "text", property: "align" });
    assert.deepEqual(completionContext("page A { column padding top = "),
        { kind: "value", element: "column", property: "padding" });
    assert.equal(completionContext("page A { column padding ").kind, "paddingSide");
    assert.deepEqual(completionContext('page A { text "Hel'), { kind: "none" });
    assert.deepEqual(completionContext("page A { # a comm"), { kind: "none" });
    assert.deepEqual(completionContext("page A { heading "), { kind: "none" });
    assert.deepEqual(completionContext("page "), { kind: "pageName" });
});
