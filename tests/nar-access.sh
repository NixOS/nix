source common.sh

echo "building test path"
storePath="$(nix-build nar-access.nix -A a --no-out-link)"

cd "$TEST_ROOT"

echo "dumping path to nar"
narFile="$TEST_ROOT/path.nar"
nix-store --dump $storePath > $narFile

echo "check that find and ls-nar match"
( cd $storePath; find . | sort ) > files.find
nix ls-nar -R -d $narFile "" | sort > files.ls-nar
diff -u files.find files.ls-nar

echo "check that file contents of data match"
nix cat-nar $narFile /foo/data > data.cat-nar
diff -u data.cat-nar $storePath/foo/data

echo "check that file contents of baz match"
nix cat-nar $narFile /foo/baz > baz.cat-nar
diff -u baz.cat-nar $storePath/foo/baz
