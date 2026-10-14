#compdef nix
# shellcheck disable=all

function _nix() {
  local ifs_bk="$IFS"
  local input=("${(Q)words[@]}")
  IFS=$'\n'
  local res=($(NIX_GET_COMPLETIONS=$((CURRENT - 1)) "$input[@]" 2>/dev/null))
  IFS="$ifs_bk"
  local type="${${res[1]}%%>	*}"
  local -a suggestions
  declare -a suggestions
  for suggestion in ${res:1}; do
    suggestions+=("${suggestion%%	*}")
  done
  local -a args
  if [[ "$type" == filenames ]]; then
    args+=('-f')
  elif [[ "$type" == attrs ]]; then
    args+=('-S' '')
  fi
  compadd -J nix "${args[@]}" -a suggestions
}

_nix "$@"
