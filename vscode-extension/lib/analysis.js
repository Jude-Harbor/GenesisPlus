"use strict";

/*
 * Pure helper functions used by the extension. They do not depend on the
 * `vscode` module, so they can be unit tested with plain Node.js.
 */

const { ELEMENTS } = require("./language");

/**
 * Parses the output of `genesis check --machine`:
 *   severity<TAB>line<TAB>column<TAB>length<TAB>message
 * Lines and columns are 1-based; columns count Unicode code points.
 */
function parseMachineDiagnostics(output) {
    const results = [];

    for (const raw of output.split(/\r?\n/)) {
        if (raw.trim() === "") {
            continue;
        }
        const parts = raw.split("\t");
        if (parts.length < 5) {
            continue;
        }
        const [severity, line, column, length] = parts;
        const message = parts.slice(4).join(" ");
        const lineNumber = Number.parseInt(line, 10);
        const columnNumber = Number.parseInt(column, 10);
        const lengthNumber = Number.parseInt(length, 10);

        if (!Number.isFinite(lineNumber) || !Number.isFinite(columnNumber)) {
            continue;
        }

        results.push({
            severity: severity === "warning" ? "warning" : "error",
            line: lineNumber,
            column: columnNumber,
            length: Number.isFinite(lengthNumber) && lengthNumber > 0 ? lengthNumber : 1,
            message,
        });
    }

    return results;
}

/**
 * Converts a 1-based code point column into a 0-based UTF-16 offset in
 * `lineText`, which is what VS Code positions use.
 */
function codePointColumnToOffset(lineText, column) {
    let offset = 0;
    let current = 1;

    for (const character of lineText) {
        if (current >= column) {
            break;
        }
        offset += character.length;
        current++;
    }

    if (current < column) {
        offset += column - current; /* past the end of the line */
    }

    return offset;
}

/** Converts a code point count starting at a UTF-16 offset into a UTF-16 length. */
function codePointLengthToUtf16(lineText, offset, length) {
    let end = offset;
    let counted = 0;

    while (end < lineText.length && counted < length) {
        const code = lineText.codePointAt(end);
        end += code > 0xffff ? 2 : 1;
        counted++;
    }

    return Math.max(1, end - offset);
}

/**
 * A small tokenizer that mirrors the compiler's lexer closely enough for
 * editor features. Unterminated strings and comments run to the end.
 */
function tokenize(text) {
    const tokens = [];
    let i = 0;

    while (i < text.length) {
        const c = text[i];

        if (/\s| |​|﻿/.test(c)) {
            i++;
            continue;
        }

        if (c === "#") {
            const end = text.indexOf("\n", i);
            const stop = end === -1 ? text.length : end;
            tokens.push({ type: "comment", text: text.slice(i, stop), start: i, end: stop });
            i = stop;
            continue;
        }

        if (c === '"') {
            let j = i + 1;
            let closed = false;
            while (j < text.length && text[j] !== "\n" && text[j] !== "\r") {
                if (text[j] === "\\" && j + 1 < text.length) {
                    j += 2;
                    continue;
                }
                if (text[j] === '"') {
                    closed = true;
                    j++;
                    break;
                }
                j++;
            }
            tokens.push({ type: "string", text: text.slice(i, j), start: i, end: j, closed });
            i = j;
            continue;
        }

        if (c === "{" || c === "}" || c === "=" || c === ";") {
            tokens.push({ type: c, text: c, start: i, end: i + 1 });
            i++;
            continue;
        }

        const word = /^[A-Za-z_][A-Za-z0-9_]*/.exec(text.slice(i, i + 64));
        if (word) {
            const value = word[0];
            tokens.push({
                type: Object.prototype.hasOwnProperty.call(ELEMENTS, value) ? "keyword" : "word",
                text: value,
                start: i,
                end: i + value.length,
            });
            i += value.length;
            continue;
        }

        const number = /^[0-9]+(\.[0-9]+)?%?/.exec(text.slice(i, i + 32));
        if (number) {
            tokens.push({ type: "number", text: number[0], start: i, end: i + number[0].length });
            i += number[0].length;
            continue;
        }

        tokens.push({ type: "other", text: c, start: i, end: i + 1 });
        i++;
    }

    return tokens;
}

/**
 * Works out what kind of completion makes sense at the end of `textBefore`.
 *
 * Returns one of:
 *   { kind: "none" }                                 inside a string or comment
 *   { kind: "statement", topLevel }                  start of a new element
 *   { kind: "properties", element, used, alsoStatement }
 *   { kind: "paddingSide", element, used }
 *   { kind: "value", element, property }
 *   { kind: "pageName" }
 */
function completionContext(textBefore) {
    const tokens = tokenize(textBefore);
    const last = tokens[tokens.length - 1];

    if (last && last.end === textBefore.length) {
        if (last.type === "string" && !last.closed) {
            return { kind: "none" };
        }
        if (last.type === "comment") {
            return { kind: "none" };
        }
    }
    if (last && last.type === "comment") {
        /* A comment runs to the end of its line. */
        const afterComment = textBefore.slice(last.end);
        if (!/[\r\n]/.test(afterComment)) {
            return { kind: "none" };
        }
    }

    const code = tokens.filter((t) => t.type !== "comment");

    /* Ignore a partially typed word at the cursor. */
    let end = code.length;
    if (end > 0) {
        const tail = code[end - 1];
        if ((tail.type === "word" || tail.type === "keyword") && tail.end === textBefore.length) {
            end--;
        }
    }

    let depth = 0;
    for (let i = 0; i < end; i++) {
        if (code[i].type === "{") depth++;
        if (code[i].type === "}") depth = Math.max(0, depth - 1);
    }

    /* Find the start of the current statement. */
    let start = end - 1;
    while (start >= 0) {
        const t = code[start];
        if (t.type === "{" || t.type === "}" || t.type === ";") {
            break;
        }
        if (t.type === "keyword") {
            break;
        }
        start--;
    }

    if (start < 0 || code[start].type !== "keyword") {
        return { kind: "statement", topLevel: depth === 0 };
    }

    const element = code[start].text;
    const spec = ELEMENTS[element];
    if (element === "page" && end === start + 1) {
        return { kind: "pageName" };
    }

    /* A page's name is not a property. */
    const rest = code.slice(start + (element === "page" ? 2 : 1), end);

    /* Elements that take text need it before any property. */
    if (spec.takesText && rest.length === 0) {
        return { kind: "none" };
    }

    const used = [];
    for (let i = 0; i < rest.length; i++) {
        const isSide = i > 0 && rest[i - 1].text === "padding";
        if (rest[i].type === "word" && !isSide && rest[i + 1] && rest[i + 1].type === "=") {
            used.push(rest[i].text);
        }
        if (rest[i].text === "padding" && rest[i + 1] && rest[i + 1].type === "word" &&
            rest[i + 2] && rest[i + 2].type === "=") {
            used.push("padding " + rest[i + 1].text);
        }
    }

    const previous = rest[rest.length - 1];
    const beforePrevious = rest[rest.length - 2];

    if (previous && previous.type === "=") {
        const property = beforePrevious && beforePrevious.type === "word" ? beforePrevious.text : null;
        const third = rest[rest.length - 3];
        const name = third && third.text === "padding" ? "padding" : property;
        return { kind: "value", element, property: name };
    }

    if (previous && previous.text === "padding" && previous.type === "word") {
        return { kind: "paddingSide", element, used };
    }

    if (previous && previous.type === "word") {
        /* A property name without '=' yet: nothing sensible to add. */
        return { kind: "none" };
    }

    return {
        kind: "properties",
        element,
        used,
        /* After a complete element, a new statement may also start. */
        alsoStatement: depth > 0 && !spec.block,
    };
}

module.exports = {
    parseMachineDiagnostics,
    codePointColumnToOffset,
    codePointLengthToUtf16,
    tokenize,
    completionContext,
};
