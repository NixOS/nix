source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping flake search tests"
    exit 99
fi

clearStore

cp ./simple.nix ./simple.builder.sh ./config.nix $TEST_HOME
cd $TEST_HOME
mkdir -p foo/subdir
echo '{ outputs = _: {}; }' > foo/flake.nix
cat <<EOF > flake.nix
{
    inputs.foo.url = "$PWD/foo";
    outputs = a: {
       packages.$system = rec {
         test = import ./simple.nix;
         default = test;
       };
    };
}
EOF
mkdir subdir
pushd subdir

success=("" . .# .#test ../subdir ../subdir#test "$PWD")
failure=("path:$PWD")

for i in "${success[@]}"; do
    nix build $i || fail "flake should be found by searching up directories"
done

for i in "${failure[@]}"; do
    ! nix build $i || fail "flake should not search up directories when using 'path:'"
done

popd

nix build --override-input foo . || fail "flake should search up directories when not an installable"

sed "s,$PWD/foo,$PWD/foo/subdir,g" -i flake.nix
! nix build || fail "flake should not search upwards when part of inputs"

pushd subdir
git init
for i in "${success[@]}" "${failure[@]}"; do
    ! nix build $i || fail "flake should not search past a git repository"
done
rm -rf .git
popd
