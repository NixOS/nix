source common.sh

echo "building test path"
storePath="$(nix-build nar-access.nix -A a --no-out-link)"

cd "$TEST_ROOT"

# Dump path to nar.
narFile="$TEST_ROOT/path.nar"
nix-store --dump $storePath > $narFile

# Check that find and ls-nar match.
( cd $storePath; find . | sort ) > files.find
nix ls-nar -R -d $narFile "" | sort > files.ls-nar
diff -u files.find files.ls-nar

# Check that file contents of data match.
nix cat-nar $narFile /foo/data > data.cat-nar
diff -u data.cat-nar $storePath/foo/data

# Check that file contents of baz match.
nix cat-nar $narFile /foo/baz > baz.cat-nar
diff -u baz.cat-nar $storePath/foo/baz

nix cat-store $storePath/foo/baz > baz.cat-nar
diff -u baz.cat-nar $storePath/foo/baz

# Test --json.
[[ $(nix ls-nar --json $narFile /) = '{"type":"directory","entries":{"foo":{},"foo-x":{},"qux":{},"zyx":{}}}' ]]
[[ $(nix ls-nar --json -R $narFile /foo) = '{"type":"directory","entries":{"bar":{"type":"regular","size":0,"narOffset":368},"baz":{"type":"regular","size":0,"narOffset":552},"data":{"type":"regular","size":58,"narOffset":736}}}' ]]
[[ $(nix ls-nar --json -R $narFile /foo/bar) = '{"type":"regular","size":0,"narOffset":368}' ]]
[[ $(nix ls-store --json $storePath) = '{"type":"directory","entries":{"foo":{},"foo-x":{},"qux":{},"zyx":{}}}' ]]
[[ $(nix ls-store --json -R $storePath/foo) = '{"type":"directory","entries":{"bar":{"type":"regular","size":0},"baz":{"type":"regular","size":0},"data":{"type":"regular","size":58}}}' ]]
[[ $(nix ls-store --json -R $storePath/foo/bar) = '{"type":"regular","size":0}' ]]

# Test missing files.
nix ls-store --json -R $storePath/xyzzy 2>&1 | grep 'does not exist in NAR'
nix ls-store $storePath/xyzzy 2>&1 | grep 'does not exist'

# Test failure to dump.
if nix-store --dump $storePath >/dev/full ; then
    echo "dumping to /dev/full should fail"
    exit -1
fi
