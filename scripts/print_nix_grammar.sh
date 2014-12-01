#!/bin/sh
#
# Print a BNF-like grammar summary of the Nix language

case "$0" in
  /*) me="$0" ;;
  *) me=$PWD/"$0" ;;
esac
mydir=$(dirname "$me")
syntax="$mydir/../src/libexpr/parser.y"

if [ ! -r "$syntax" ]; then
  echo "Can't find libexpr/parser.y" >&2
  exit 1
fi

nix-shell -p bison --pure --command "
  dir=\$(mktemp -d /tmp/parserXXX)
  [ \$? -eq 0 ] || exit 1

  cd \"\$dir\"
  if bison -v \"$syntax\"; then
    awk 'n==2&&\$1==\"Terminals,\"{n=0; exit 0}n==1&&\$1>=1{n=2}n==2{print substr(\$0,7)}\$0==\"Grammar\"{n=1}' parser.output
  fi
  cd
  rm -r \"\$dir\"
"
