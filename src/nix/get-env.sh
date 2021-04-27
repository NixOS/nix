set -e
if [ -e .attrs.sh ]; then source .attrs.sh; fi

export IN_NIX_SHELL=impure
export dontAddDisableDepTrack=1

if [[ -n $stdenv ]]; then
    source $stdenv/setup
fi

for __output in $outputs; do
    if [[ -z $__done ]]; then
        export > ${!__output}
        set >> ${!__output}
        __done=1
    else
        echo -n >> ${!__output}
    fi
done
