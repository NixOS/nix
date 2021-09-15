function _complete_nix {
    local -a words
    local cword cur
    _get_comp_words_by_ref -n ':=&' words cword cur
    local have_type
    while IFS= read -r line; do
        local completion=${line%%	*}
        if [[ -z $have_type ]]; then
            have_type=1
            if [[ $completion = filenames ]]; then
                compopt -o filenames
            fi
        else
            COMPREPLY+=("$completion")
        fi
    done < <(NIX_GET_COMPLETIONS=$cword "${words[@]}")
    __ltrim_colon_completions "$cur"
}

complete -F _complete_nix nix
