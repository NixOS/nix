# shellcheck shell=bash

# Values, `:doc` and the debugger's output are styled on a terminal, or when
# forced. The other tests in this suite check that piped output is plain.

esc=$'\x1b'

expectColored () {
    local what="$1"; shift
    local out
    out="$(CLICOLOR_FORCE=1 "$@" 2>/dev/null)"
    [[ "$out" == *"$esc"* ]] || fail "$what was not colored with CLICOLOR_FORCE=1: $out"
}

expectColored "a value" nix repl <<< '{ a = 1; }'
expectColored ":doc" nix repl <<< ':doc builtins.head'

echo 'let x = 1; in builtins.break x' > "$TEST_ROOT/debugger-color.nix"
expectColored ":bt" nix eval --file "$TEST_ROOT/debugger-color.nix" --debugger < <(printf ':bt\n:c\n')
expectColored ":env" nix eval --file "$TEST_ROOT/debugger-color.nix" --debugger < <(printf ':env\n:c\n')
