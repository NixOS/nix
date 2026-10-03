# shellcheck shell=bash
# shellcheck disable=SC2154
mkdir "$out"
mkdir "$out"/bin
echo "#! $builder" > "$out"/bin/"$progName"
# shellcheck disable=SC2154
echo "echo $name" >> "$out"/bin/"$progName"
chmod +x "$out"/bin/"$progName"
