# shellcheck shell=bash
function _complete_nix {
    local -a words
    local cword cur
    _get_comp_words_by_ref -n ':=&' words cword cur
    local have_type
    while IFS= read -r line; do
        local completion=${line%%	*}
        if [[ -z $have_type ]]; then
            have_type=1
            if [[ $completion == filenames ]]; then
                compopt -o filenames
            elif [[ $completion == attrs ]]; then
                compopt -o nospace
            fi
            continue
        fi

        if [[ "${cur}" =~ "=" ]]; then
            # drop everything up to the first =. if a = is included, bash assumes this to be
            # an arg=value argument and the completion gets mangled (see #11208)
            completion="${completion#*=}"
        fi

        COMPREPLY+=("${completion}")
    done < <(NIX_GET_COMPLETIONS=$cword "${words[@]}" 2>/dev/null)
    __ltrim_colon_completions "$cur"
}

complete -F _complete_nix nix
