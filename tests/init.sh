source common.sh

echo "NIX_STORE_DIR=$NIX_STORE_DIR NIX_DB_DIR=$NIX_DB_DIR"

test -n "$TEST_ROOT"
if test -d "$TEST_ROOT"; then
    chmod -R u+w "$TEST_ROOT"
    rm -rf "$TEST_ROOT"
fi
mkdir "$TEST_ROOT"

mkdir "$NIX_STORE_DIR"
mkdir "$NIX_LOCALSTATE_DIR"
mkdir -p "$NIX_LOG_DIR"/drvs
mkdir "$NIX_STATE_DIR"
mkdir "$NIX_DB_DIR"
mkdir "$NIX_CONF_DIR"

mkdir $NIX_BIN_DIR
ln -s $TOP/src/nix-store/nix-store $NIX_BIN_DIR/
ln -s $TOP/src/nix-instantiate/nix-instantiate $NIX_BIN_DIR/
ln -s $TOP/src/nix-hash/nix-hash $NIX_BIN_DIR/
ln -s $TOP/src/nix-env/nix-env $NIX_BIN_DIR/
ln -s $TOP/src/nix-worker/nix-worker $NIX_BIN_DIR/
ln -s $TOP/src/bsdiff-*/bsdiff $NIX_BIN_DIR/
ln -s $TOP/src/bsdiff-*/bspatch $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-prefetch-url $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-build $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-pull $NIX_BIN_DIR/
mkdir -p $NIX_BIN_DIR/nix/substituters
ln -s $NIX_BZIP2 $NIX_BIN_DIR/nix/
ln -s $TOP/scripts/copy-from-other-stores.pl $NIX_BIN_DIR/nix/substituters

cat > "$NIX_CONF_DIR"/nix.conf <<EOF
gc-keep-outputs = false
gc-keep-derivations = false
env-keep-derivations = false
fsync-metadata = false
EOF

# An uberhack for Mac OS X 10.5: download-using-manifests uses Perl,
# and Perl links against Darwin's libutil.dylib (in /usr/lib), but
# when running "make check", the libtool wrapper script around the Nix
# binaries sets DYLD_LIBRARY_PATH so that Perl finds Nix's (completely
# different) libutil --- so it barfs.  So generate a shell wrapper
# around download-using-manifests that clears DYLD_LIBRARY_PATH.
cat > $NIX_BIN_DIR/nix/substituters/download-using-manifests.pl <<EOF
#! $SHELL -e
export DYLD_LIBRARY_PATH=
exec $TOP/scripts/download-using-manifests.pl "\$@"
EOF
chmod +x $NIX_BIN_DIR/nix/substituters/download-using-manifests.pl

# Initialise the database.
nix-store --init

# Did anything happen?
test -e "$NIX_DB_DIR"/db.sqlite

echo 'Hello World' > ./dummy
