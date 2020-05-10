function _complete_nix {
    local have_type
    while IFS= read -r line; do
        if [[ -z $have_type ]]; then
            have_type=1
            if [[ $line = filenames ]]; then
                compopt -o filenames
            fi
        else
            COMPREPLY+=("$line")
        fi
    done < <(NIX_GET_COMPLETIONS=$COMP_CWORD "${COMP_WORDS[@]}")
}

complete -F _complete_nix nix
