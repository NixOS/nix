source ./common.sh

flake1Dir=$TEST_ROOT/flake1

mkdir -p $flake1Dir

pwd=$(pwd)

cat > $flake1Dir/flake.nix <<EOF
{
  inputs.docs = {
    url = "path://$pwd/..";
    flake = false;
  };

  outputs = { self, docs }: with import ./lib.nix; {

    shOnly = builtins.filterPath {
      path = docs;
      filter =
        path: type:
        (type != "regular" || hasSuffix ".sh" path);
    };

    lang = builtins.filterPath {
      path = self.shOnly + "/lang";
      filter =
        path: type:
        assert !hasPrefix "/lang" path;
        (builtins.baseNameOf path != "readDir");
    };

  };
}
EOF

cp ../lang/lib.nix $flake1Dir/

nix build --out-link $TEST_ROOT/result $flake1Dir#shOnly
[[ -e $TEST_ROOT/result/flakes/tree-operators.sh ]]
(! find $TEST_ROOT/result/ -type f | grep -v '.sh$')

nix build --out-link $TEST_ROOT/result $flake1Dir#lang
[[ -e $TEST_ROOT/result/dir1 ]]
(! [[ -e $TEST_ROOT/result/readDir ]])
