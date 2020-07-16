set -e
if [ -e .attrs.sh ]; then source .attrs.sh; fi

outputs=$_outputs_saved
for __output in $_outputs_saved; do
    declare "$__output"="$out"
done
unset _outputs_saved __output

export IN_NIX_SHELL=impure
export dontAddDisableDepTrack=1

if [[ -n $stdenv ]]; then
    source $stdenv/setup
fi

export > $out
set >> $out
