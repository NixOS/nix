. $stdenv/setup

mainName=$(basename $main | cut -c34-)

echo "compiling \`$mainName'..."

# Turn $localIncludes into an array.
localIncludes=($localIncludes)

# Determine how many `..' levels appear in the header file references.
# E.g., if there is some reference `../../foo.h', then we have to
# insert two extra levels in the directory structure, so that `a.c' is
# stored at `dotdot/dotdot/a.c', and a reference from it to
# `../../foo.h' resolves to `dotdot/dotdot/../../foo.h' == `foo.h'.
n=0
maxDepth=0
for ((n = 0; n < ${#localIncludes[*]}; n += 2)); do
    target=${localIncludes[$((n + 1))]}

    # Split the target name into path components using some IFS magic.
    savedIFS="$IFS"
    IFS=/
    components=($target)
    depth=0
    for ((m = 0; m < ${#components[*]}; m++)); do
        c=${components[m]}
        if test "$c" = ".."; then
            depth=$((depth + 1))
        fi
    done
    IFS="$savedIFS"

    if test $depth -gt $maxDepth; then
        maxDepth=$depth;
    fi
done

# Create the extra levels in the directory hierarchy.
prefix=
for ((n = 0; n < maxDepth; n++)); do
    prefix="dotdot/$prefix"
done

# Create symlinks to the header files.
for ((n = 0; n < ${#localIncludes[*]}; n += 2)); do
    source=${localIncludes[n]}
    target=${localIncludes[$((n + 1))]}

    # Create missing directories.  We use IFS magic to split the path
    # into path components.
    savedIFS="$IFS"
    IFS=/
    components=($prefix$target)
    fullPath=(.)
    for ((m = 0; m < ${#components[*]} - 1; m++)); do
        fullPath=("${fullPath[@]}" ${components[m]})
        if ! test -d "${fullPath[*]}"; then
            mkdir "${fullPath[*]}"
        fi
    done
    IFS="$savedIFS"
    
    ln -sf $source $prefix$target
done

# Create a symlink to the main file.
if ! test "$(readlink $prefix$mainName)" = $main; then
    ln -s $main $prefix$mainName
fi

mkdir $out
test "$prefix" && cd $prefix
gcc -Wall $cFlags -c $mainName -o $out/$mainName.o
