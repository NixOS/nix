function _complete_nix {
    while IFS= read -r line; do
        COMPREPLY+=("$line")
    done < <(NIX_GET_COMPLETIONS=$COMP_CWORD "${COMP_WORDS[@]}")
}

complete -F _complete_nix nix
