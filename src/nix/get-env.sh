set -e
if [ -e .attrs.sh ]; then source .attrs.sh; fi
export IN_NIX_SHELL=impure
export dontAddDisableDepTrack=1
if [[ -n $stdenv ]]; then
    source $stdenv/setup
fi
export > $out
set >> $out
