. $stdenv/setup

mainName=$(basename $main | cut -c34-)
ln -s $main $mainName

echo "compiling $mainName..."

localIncludes=($localIncludes)
n=0
while test $n -lt ${#localIncludes[*]}; do
    source=${localIncludes[n]}
    target=${localIncludes[$((n+1))]}
    ln -s $source $target
    n=$((n + 2))
done

mkdir $out
gcc -Wall -c $mainName -o $out/$mainName.o
