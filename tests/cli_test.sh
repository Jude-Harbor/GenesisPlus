#!/bin/sh
# End-to-end tests for the genesis command-line tool.
#
# Usage: sh tests/cli_test.sh ./genesis      (or ./genesis.exe on Windows)
#
# Runs on Linux, macOS, and Windows (MSYS2 / Git Bash). The dev server tests
# need curl; they are reported as SKIPPED if it is not installed.

set -u

if [ $# -lt 1 ]; then
    echo "usage: $0 path/to/genesis" >&2
    exit 2
fi

ROOT=$(pwd)
case "$1" in
    /*) GENESIS="$1" ;;
    *) GENESIS="$ROOT/$1" ;;
esac

if [ ! -x "$GENESIS" ]; then
    echo "error: '$GENESIS' is not an executable" >&2
    exit 2
fi

# Refuse to test a binary that does not run here (for example a Windows
# genesis.exe on Linux) instead of producing hundreds of confusing failures.
if ! VERSION_OUTPUT=$("$GENESIS" --version 2>&1) || ! echo "$VERSION_OUTPUT" | grep -q '^Genesis+ '; then
    echo "error: '$GENESIS' is not a working Genesis+ compiler for this platform:" >&2
    echo "$VERSION_OUTPUT" >&2
    exit 2
fi
echo "Testing $GENESIS ($VERSION_OUTPUT)"

# All tests run inside a temporary folder whose path contains spaces.
BASE=$(mktemp -d 2>/dev/null || mktemp -d -t genesis)
WORK="$BASE/work dir with spaces"
mkdir -p "$WORK" || exit 2
PASSED=0
FAILED=0
SKIPPED=0
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill -9 "$SERVER_PID" 2>/dev/null
    fi
    cd "$ROOT" || exit 1
    rm -rf "$BASE"
}
trap cleanup EXIT

pass() {
    PASSED=$((PASSED + 1))
}

fail() {
    FAILED=$((FAILED + 1))
    echo "  FAIL: $1"
    if [ -f "$WORK/out.txt" ]; then
        sed 's/^/    | /' "$WORK/out.txt" | head -20
    fi
}

# run EXPECTED_EXIT DESCRIPTION COMMAND... (output captured in $WORK/out.txt)
run() {
    expected=$1
    description=$2
    shift 2
    "$@" >"$WORK/out.txt" 2>&1
    status=$?
    if [ "$status" -eq "$expected" ]; then
        pass
    else
        fail "$description (exit $status, expected $expected)"
    fi
}

contains() {
    if grep -F -q -- "$2" "$1"; then
        pass
    else
        fail "$3 ('$2' not found in $1)"
    fi
}

not_contains() {
    if grep -F -q -- "$2" "$1"; then
        fail "$3 ('$2' unexpectedly found in $1)"
    else
        pass
    fi
}

exists() {
    if [ -e "$1" ]; then pass; else fail "$2 ($1 missing)"; fi
}

missing() {
    if [ -e "$1" ]; then fail "$2 ($1 should not exist)"; else pass; fi
}

same_file() {
    if cmp -s "$1" "$2"; then pass; else fail "$3 ($1 and $2 differ)"; fi
}

checksum() {
    cksum <"$1" 2>/dev/null
}

cd "$WORK" || exit 1

echo "== general commands"
run 0 "--version" "$GENESIS" --version
contains out.txt "Genesis+ " "--version prints the version"
run 0 "-v" "$GENESIS" -v
run 0 "--help" "$GENESIS" --help
contains out.txt "genesis build" "--help lists commands"
run 0 "help build" "$GENESIS" help build
run 0 "build --help" "$GENESIS" build --help
run 2 "no command" "$GENESIS"
run 2 "unknown command" "$GENESIS" frobnicate
contains out.txt "unknown command" "unknown command message"
run 2 "bare .gns argument" "$GENESIS" lead.gns
contains out.txt "genesis build lead.gns" "suggests build"
run 2 "unknown option" "$GENESIS" build --frob
run 2 "too many arguments" "$GENESIS" build a.gns b.gns
run 2 "invalid port" "$GENESIS" dev --port 80
run 2 "port not a number" "$GENESIS" dev --port abc
run 2 "help for unknown command" "$GENESIS" help nope

echo "== init"
run 0 "init new folder" "$GENESIS" init site
exists site/lead.gns "init creates lead.gns"
exists site/assets "init creates assets/"
exists site/.gitignore "init creates .gitignore"
echo "# my edits" >>site/lead.gns
before=$(checksum site/lead.gns)
run 0 "init existing folder" "$GENESIS" init site
contains out.txt "already exists" "init reports existing file"
after=$(checksum site/lead.gns)
if [ "$before" = "$after" ]; then pass; else fail "init must not overwrite lead.gns"; fi
echo "not a folder" >afile
run 1 "init on a file" "$GENESIS" init afile

echo "== build"
run 1 "build without lead.gns" "$GENESIS" build
contains out.txt "No lead.gns found" "explains missing lead.gns"
run 0 "build folder" "$GENESIS" build site
exists site/dist/index.html "index.html generated"
exists site/dist/style.css "style.css generated"
contains site/dist/index.html "<h1 style=" "starter heading rendered"
missing site/dist/index.html.genesis-tmp "no temp files left behind"
(cd site && "$GENESIS" build >"$WORK/out.txt" 2>&1)
if [ $? -eq 0 ]; then pass; else fail "build with default entry inside project"; fi
run 0 "build file path" "$GENESIS" build site/lead.gns
run 1 "build missing file" "$GENESIS" build nope.gns
contains out.txt "does not exist" "missing file message"
echo "x" >notgenesis.txt
run 1 "build non-.gns file" "$GENESIS" build notgenesis.txt
contains out.txt "not a Genesis+ file" "wrong extension message"

echo "== failed builds leave existing output alone"
mkdir -p proj
cat >proj/lead.gns <<'EOF'
page Home {
    heading "Version one"
}
EOF
run 0 "first build" "$GENESIS" build proj
echo "keep me" >proj/dist/notes.txt
cp proj/dist/index.html good.html
cat >proj/lead.gns <<'EOF'
page Home {
    column {
        heading "Version two" size = 5000
EOF
run 1 "broken build fails" "$GENESIS" build proj
contains out.txt "error" "error is reported"
contains out.txt "No files were written" "explains nothing was written"
contains out.txt "line 3" "reports a line number"
same_file proj/dist/index.html good.html "previous output untouched after failure"
exists proj/dist/notes.txt "unrelated files in dist/ are kept"
cat >proj/lead.gns <<'EOF'
page Home {
    heading "Version three"
}
EOF
run 0 "rebuild after fix" "$GENESIS" build proj
contains proj/dist/index.html "Version three" "output updated"
exists proj/dist/notes.txt "unrelated files survive a rebuild"

echo "== repeated builds, clean rebuilds, absolute paths, sources untouched"
mkdir -p again/assets
printf 'svg' >again/assets/a.svg
printf 'page Home {
    image "assets/a.svg" alt = "A"
}
page Two {
    text "2"
}
' >again/lead.gns
source_before=$(checksum again/lead.gns)
run 0 "build once" "$GENESIS" build again
cp again/dist/index.html again_first.html
cp again/dist/two.html again_two.html
run 0 "build again" "$GENESIS" build again
same_file again/dist/index.html again_first.html "repeated build is identical"
same_file again/dist/two.html again_two.html "repeated build is identical (page 2)"
rm -rf again/dist
run 0 "clean rebuild" "$GENESIS" build again
same_file again/dist/index.html again_first.html "clean rebuild is identical"
exists again/dist/assets/a.svg "clean rebuild copies assets"
run 0 "absolute file path" "$GENESIS" build "$WORK/again/lead.gns"
run 0 "absolute folder path" "$GENESIS" check "$WORK/again"
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        run 0 "Windows backslash path" "$GENESIS" build 'again\lead.gns'
        same_file again/dist/index.html again_first.html "backslash path builds the same site"
        ;;
esac
source_after=$(checksum again/lead.gns)
if [ "$source_before" = "$source_after" ]; then pass; else fail "build must not modify the source file"; fi
missing again/lead.gns.genesis-tmp "no temp file next to the source"

echo "== dist/ that is a file"
mkdir -p blocked
printf 'page A { text "x" }\n' >blocked/lead.gns
echo "I am a file" >blocked/dist
run 1 "dist is a file" "$GENESIS" build blocked
contains out.txt "is a file" "explains dist problem"

echo "== whitespace and line endings"
mkdir -p ws_lf ws_crlf ws_compact ws_tabs
printf 'page Home {\n    column gap = 8 {\n        heading "Hi" size = 30\n        text "There"\n    }\n}\n' >ws_lf/lead.gns
printf 'page Home {\r\n    column gap = 8 {\r\n        heading "Hi" size = 30\r\n        text "There"\r\n    }\r\n}\r\n' >ws_crlf/lead.gns
printf 'page Home{column gap=8{heading "Hi" size=30 text "There"}}' >ws_compact/lead.gns
printf 'page Home {\n\tcolumn gap = 8 {\n\t\theading "Hi" size = 30\n  \t  text "There"\n\t}\n}\n' >ws_tabs/lead.gns
for d in ws_lf ws_crlf ws_compact ws_tabs; do
    run 0 "build $d" "$GENESIS" build $d
done
same_file ws_lf/dist/index.html ws_crlf/dist/index.html "CRLF output matches LF"
same_file ws_lf/dist/index.html ws_compact/dist/index.html "compact output matches"
same_file ws_lf/dist/index.html ws_tabs/dist/index.html "tab/space mix output matches"

echo "== legacy indentation syntax gives a migration hint"
mkdir -p legacy
printf 'page Home:\n    column:\n        text "old"\n' >legacy/lead.gns
run 1 "legacy syntax rejected" "$GENESIS" build legacy
contains out.txt "now uses braces" "legacy hint shown"

echo "== multiple pages and assets"
mkdir -p multi/assets/img
printf 'fake png' >multi/assets/img/logo.png
cat >multi/lead.gns <<'EOF'
page Home {
    image "assets/img/logo.png" alt = "Logo"
    link "About" to = "about.html"
}
page About {
    text "About us"
}
EOF
run 0 "multi-page build" "$GENESIS" build multi
exists multi/dist/index.html "index.html"
exists multi/dist/about.html "about.html"
exists multi/dist/assets/img/logo.png "assets copied recursively"
contains out.txt "assets/ (copied)" "build reports asset copy"
printf 'page Home { image "assets/missing.png" alt = "x" }\n' >multi/lead.gns
run 1 "missing image fails" "$GENESIS" build multi
contains out.txt "was not found" "missing image message"

echo "== check"
run 0 "check valid" "$GENESIS" check ws_lf
contains out.txt "no problems found" "check success message"
run 1 "check invalid" "$GENESIS" check proj/../legacy
run 1 "check machine format" "$GENESIS" check legacy --machine
contains out.txt "error	1	10	1	" "machine output fields"
printf 'page A { column { } }' >warn.gns
run 0 "warnings do not fail check" "$GENESIS" check warn.gns
contains out.txt "1 warning" "warning count"
printf 'page A { text "x" size = 3 }' | "$GENESIS" check --stdin --machine >out.txt 2>&1
if [ $? -eq 1 ]; then pass; else fail "check --stdin invalid"; fi
contains out.txt "error	1	26	1	Invalid size 3" "stdin machine diagnostics"
printf 'page A { text "ok" }' | "$GENESIS" check --stdin >out.txt 2>&1
if [ $? -eq 0 ]; then pass; else fail "check --stdin valid"; fi
printf 'page A { image "assets/img/logo.png" alt = "x" }' | "$GENESIS" check --stdin --stdin-filename multi/lead.gns >out.txt 2>&1
if [ $? -eq 0 ]; then pass; else fail "check --stdin-filename resolves images"; fi
run 2 "check file and --stdin" "$GENESIS" check a.gns --stdin
missing legacy/dist "check never writes output"

echo "== fmt"
mkdir -p fmt
printf 'page Home{column gap=8{heading "Hi" text "There"}}' >fmt/lead.gns
run 1 "fmt --check unformatted" "$GENESIS" fmt --check fmt
contains out.txt "needs formatting" "fmt --check message"
run 0 "fmt rewrites" "$GENESIS" fmt fmt
run 0 "fmt --check formatted" "$GENESIS" fmt --check fmt
printf 'page Home {\n    column gap = 8 {\n        heading "Hi"\n        text "There"\n    }\n}\n' >expected_fmt.gns
same_file fmt/lead.gns expected_fmt.gns "fmt output"
printf 'page A{text "x"}' | "$GENESIS" fmt --stdin >out.txt 2>&1
printf 'page A {\n    text "x"\n}\n' >expected_stdin.gns
same_file out.txt expected_stdin.gns "fmt --stdin output"
cp legacy/lead.gns legacy_copy.gns
run 1 "fmt refuses invalid file" "$GENESIS" fmt legacy
same_file legacy/lead.gns legacy_copy.gns "invalid file left unchanged"

echo "== examples"
for example in "$ROOT"/examples/*/lead.gns; do
    [ -f "$example" ] || continue
    name=$(basename "$(dirname "$example")")
    rm -rf "$WORK/ex_$name"
    cp -R "$(dirname "$example")" "$WORK/ex_$name"
    run 0 "example $name builds" "$GENESIS" build "$WORK/ex_$name"
    not_contains out.txt "warning" "example $name has no warnings"
    exists "$WORK/ex_$name/dist/index.html" "example $name index.html"
    run 0 "example $name is formatted" "$GENESIS" fmt --check "$WORK/ex_$name"
done

echo "== documentation examples"
# Every ```gns block in the docs must compile. ```gns-snippet blocks are
# fragments and are wrapped in a page first.
mkdir -p docs_proj/assets
for asset in portrait.jpg photo.jpg logo.svg; do
    printf 'x' >"docs_proj/assets/$asset"
done
for doc in "$ROOT/README.md" "$ROOT"/docs/*.md; do
    [ -f "$doc" ] || continue
    rm -f "$WORK"/docblock_*
    awk -v out="$WORK/docblock" '
        /^```gns(-snippet)?[ \t\r]*$/ {
            n++
            kind = ($0 ~ /snippet/) ? "snippet" : "file"
            file = out "_" n "." kind
            printf "" > file
            inblock = 1
            next
        }
        /^```/ && inblock { inblock = 0; close(file); next }
        inblock { print > file }
    ' "$doc"
    for block in "$WORK"/docblock_*; do
        [ -f "$block" ] || continue
        case "$block" in
            *.snippet) { echo "page Snippet {"; cat "$block"; echo "}"; } >docs_proj/lead.gns ;;
            *) cp "$block" docs_proj/lead.gns ;;
        esac
        run 0 "example $(basename "$block") in $(basename "$doc") compiles" "$GENESIS" check docs_proj
    done
done
rm -f "$WORK"/docblock_*

echo "== dev server"
if ! command -v curl >/dev/null 2>&1; then
    echo "  SKIPPED: curl is not installed"
    SKIPPED=$((SKIPPED + 1))
else
    PORT=$((20000 + $$ % 20000))
    mkdir -p dev/assets
    printf 'body{}' >dev/assets/site.css
    printf 'page Home {\n    heading "First"\n}\npage About {\n    text "About"\n}\n' >dev/lead.gns
    "$GENESIS" dev dev --port "$PORT" >dev_out.txt 2>&1 &
    SERVER_PID=$!
    up=0
    i=0
    while [ $i -lt 50 ]; do
        if curl -s -m 2 -o /dev/null "http://127.0.0.1:$PORT/__genesis/version"; then
            up=1
            break
        fi
        sleep 0.2
        i=$((i + 1))
    done

    if [ $up -ne 1 ]; then
        fail "dev server did not start"
        cat dev_out.txt
    else
        pass
        URL="http://127.0.0.1:$PORT"
        code() { curl -s -m 5 -o /dev/null -w '%{http_code}' --path-as-is "$@"; }

        [ "$(code "$URL/")" = "200" ] && pass || fail "GET / is 200"
        curl -s -m 5 "$URL/" >page.html
        contains page.html "First" "serves index.html"
        contains page.html "/__genesis/version" "live reload script injected"
        not_contains dev/dist/index.html "__genesis" "dist/ is not modified by the server"
        [ "$(code "$URL/about")" = "200" ] && pass || fail "/about maps to about.html"
        [ "$(code "$URL/about.html")" = "200" ] && pass || fail "GET /about.html"
        [ "$(code "$URL/assets/site.css")" = "200" ] && pass || fail "GET asset"
        curl -s -m 5 -D headers.txt -o /dev/null "$URL/style.css"
        contains headers.txt "text/css" "CSS content type"
        contains headers.txt "nosniff" "nosniff header"
        [ "$(code "$URL/nope.html")" = "404" ] && pass || fail "missing page is 404"
        [ "$(code "$URL/../lead.gns")" = "400" ] && pass || fail "traversal is rejected"
        [ "$(code "$URL/%2e%2e/lead.gns")" = "400" ] && pass || fail "encoded traversal is rejected"
        [ "$(code "$URL/..%5clead.gns")" = "400" ] && pass || fail "backslash traversal is rejected"
        [ "$(code "$URL/con")" = "400" ] && pass || fail "device names are rejected"
        [ "$(code -X POST "$URL/")" = "405" ] && pass || fail "POST is 405"
        [ "$(code -I "$URL/")" = "200" ] && pass || fail "HEAD is 200"
        [ "$(code --request-target "nopath" "$URL/")" = "400" ] && pass || fail "target without / is 400"
        [ "$(code --request-target "/%zz" "$URL/")" = "400" ] && pass || fail "bad percent-encoding is 400"
        [ "$(code --request-target "/a%00b" "$URL/")" = "400" ] && pass || fail "encoded NUL is 400"
        long_target="/$(printf '%03000d' 0)"
        [ "$(code --request-target "$long_target" "$URL/")" = "414" ] && pass || fail "very long target is 414"
        long_header=$(printf '%09000d' 0)
        [ "$(code -H "X-Big: $long_header" "$URL/")" = "431" ] && pass || fail "huge headers are 431"
        [ "$(code "$URL/assets/../../lead.gns")" = "400" ] && pass || fail "traversal via assets/ is rejected"
        [ "$(code "$URL/style.css")" = "200" ] && pass || fail "server still healthy after bad requests"

        v1=$(curl -s -m 5 "$URL/__genesis/version")
        sleep 1
        printf 'page Home {\n    heading "Second"\n}\n' >dev/lead.gns
        changed=0
        i=0
        while [ $i -lt 40 ]; do
            v2=$(curl -s -m 5 "$URL/__genesis/version")
            if [ "$v2" != "$v1" ]; then
                changed=1
                break
            fi
            sleep 0.25
            i=$((i + 1))
        done
        [ $changed -eq 1 ] && pass || fail "rebuild after source change"
        curl -s -m 5 "$URL/" >page.html
        contains page.html "Second" "rebuilt page is served"

        sleep 1
        printf 'page Home {\n    heading "Broken" size = 1\n}\n' >dev/lead.gns
        i=0
        status=""
        while [ $i -lt 40 ]; do
            status=$(code "$URL/")
            [ "$status" = "500" ] && break
            sleep 0.25
            i=$((i + 1))
        done
        [ "$status" = "500" ] && pass || fail "broken build shows an error page"
        curl -s -m 5 "$URL/" >page.html
        contains page.html "Invalid size 1" "error page shows the compiler error"
        kill -0 "$SERVER_PID" 2>/dev/null && pass || fail "server survives a compile error"

        "$GENESIS" dev dev --port "$PORT" >busy.txt 2>&1 &
        busy_pid=$!
        sleep 1.5
        if kill -0 "$busy_pid" 2>/dev/null; then
            kill "$busy_pid" 2>/dev/null
            fail "second server on a busy port should exit"
        else
            wait "$busy_pid"
            [ $? -eq 1 ] && pass || fail "busy port exits with status 1"
            contains busy.txt "already in use" "busy port message"
        fi
    fi

    case "$(uname -s)" in
        MINGW* | MSYS* | CYGWIN*)
            # MSYS cannot deliver Ctrl+C to a native Windows program, so the
            # graceful-shutdown check only runs on Linux and macOS.
            kill -9 "$SERVER_PID" 2>/dev/null
            wait "$SERVER_PID" 2>/dev/null
            echo "  NOTE: graceful Ctrl+C shutdown is not tested on Windows"
            ;;
        *)
            kill -INT "$SERVER_PID" 2>/dev/null
            i=0
            while kill -0 "$SERVER_PID" 2>/dev/null && [ $i -lt 20 ]; do
                sleep 0.25
                i=$((i + 1))
            done
            if kill -0 "$SERVER_PID" 2>/dev/null; then
                kill -9 "$SERVER_PID" 2>/dev/null
                fail "server did not stop after Ctrl+C (SIGINT)"
            else
                wait "$SERVER_PID"
                [ $? -eq 0 ] && pass || fail "server exits with status 0 after Ctrl+C"
                contains dev_out.txt "dev server stopped" "graceful shutdown message"
            fi
            ;;
    esac
    SERVER_PID=""
fi

echo
echo "CLI tests: $PASSED passed, $FAILED failed, $SKIPPED skipped."
[ $FAILED -eq 0 ]
