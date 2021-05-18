set -e
if [ -e .attrs.sh ]; then source .attrs.sh; fi

export IN_NIX_SHELL=impure
export dontAddDisableDepTrack=1

if [[ -n $stdenv ]]; then
    source $stdenv/setup
fi

# In case of `__structuredAttrs = true;` the list of outputs is an associative
# array with a format like `outname => /nix/store/hash-drvname-outname`, so `__olist`
# must contain the array's keys (hence `${!...[@]}`) in this case.
if [ -e .attrs.sh ]; then
    __olist="${!outputs[@]}"
else
    __olist=$outputs
fi

for __output in $__olist; do
    if [[ -z $__done ]]; then
        export > "${!__output}"
        set >> "${!__output}"
        __done=1
    else
        echo -n >> "${!__output}"
    fi
done
