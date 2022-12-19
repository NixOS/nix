source ./common.sh

flake1Dir=$TEST_ROOT/flake1

mkdir -p $flake1Dir

pwd=$(pwd)

cat > $flake1Dir/flake.nix <<EOF
{
  inputs.docs = {
    url = "path://$pwd/../../doc";
    flake = false;
  };

  outputs = { self, docs }: with import ./lib.nix; {

    mdOnly = builtins.filterPath {
      path = docs;
      filter =
        path: type:
        (type != "regular" || hasSuffix ".md" path);
    };

    noReleaseNotes = builtins.filterPath {
      path = self.mdOnly + "/manual";
      filter =
        path: type:
        assert !hasPrefix "/manual" path;
        (builtins.baseNameOf path != "release-notes");
    };

  };
}
EOF

cp ../lang/lib.nix $flake1Dir/

nix build --out-link $TEST_ROOT/result $flake1Dir#mdOnly
[[ -e $TEST_ROOT/result/manual/src/quick-start.md ]]
[[ -e $TEST_ROOT/result/manual/src/release-notes ]]
(! find $TEST_ROOT/result/ -type f | grep -v '.md$')
find $TEST_ROOT/result/ -type f | grep release-notes

nix build --out-link $TEST_ROOT/result $flake1Dir#noReleaseNotes
[[ -e $TEST_ROOT/result/src/quick-start.md ]]
(! [[ -e $TEST_ROOT/result/src/release-notes ]])
(! find $TEST_ROOT/result/ -type f | grep release-notes)
