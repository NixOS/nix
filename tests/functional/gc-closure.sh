source common.sh

nix_gc_closure() {
    clearStore
    nix build -f dependencies.nix input0_drv --out-link $TEST_ROOT/gc-root
    input0=$(realpath $TEST_ROOT/gc-root)
    input1=$(nix build -f dependencies.nix input1_drv --no-link --print-out-paths)
    input2=$(nix build -f dependencies.nix input2_drv --no-link --print-out-paths)
    top=$(nix build -f dependencies.nix --no-link --print-out-paths)
    somthing_else=$(nix store add-path ./dependencies.nix)

    # Check that nix store gc is best-effort (doesn't fail when some paths in the closure are alive)
    nix store gc "$top"
    [[ ! -e "$top" ]] || fail "top should have been deleted"
    [[ -e "$input0" ]] || fail "input0 is a gc root, shouldn't have been deleted"
    [[ ! -e "$input2" ]] || fail "input2 is not a gc root and is part of top's closure, it should have been deleted"
    [[ -e "$input1" ]] || fail "input1 is not ins the closure of top, it shouldn't have been deleted"
    [[ -e "$somthing_else" ]] || fail "somthing_else is not in the closure of top, it shouldn't have been deleted"
}

nix_gc_closure
