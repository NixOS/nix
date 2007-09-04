source common.sh

echo "NIX_STORE_DIR=$NIX_STORE_DIR NIX_DB_DIR=$NIX_DB_DIR"

test -n "$TEST_ROOT"
if test -d "$TEST_ROOT"; then
    chmod -R u+w "$TEST_ROOT"
    rm -rf "$TEST_ROOT"
fi
mkdir "$TEST_ROOT"

mkdir "$NIX_STORE_DIR"
mkdir "$NIX_DATA_DIR"
mkdir "$NIX_LOCALSTATE_DIR"
mkdir -p "$NIX_LOG_DIR"/drvs
mkdir "$NIX_STATE_DIR"
mkdir "$NIX_DB_DIR"
mkdir "$NIX_CONF_DIR"

mkdir $NIX_BIN_DIR
ln -s $nixstore $NIX_BIN_DIR/
ln -s $nixinstantiate $NIX_BIN_DIR/
ln -s $nixhash $NIX_BIN_DIR/
ln -s $nixenv $NIX_BIN_DIR/
ln -s $nixworker $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-prefetch-url $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-collect-garbage $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-build $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-install-package $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-push $NIX_BIN_DIR/
ln -s $TOP/scripts/nix-pull $NIX_BIN_DIR/
ln -s $bzip2_bin_test/bzip2 $NIX_BIN_DIR/
ln -s $bzip2_bin_test/bunzip2 $NIX_BIN_DIR/
mkdir $NIX_BIN_DIR/nix
ln -s $TOP/scripts/download-using-manifests.pl $NIX_BIN_DIR/nix/
ln -s $TOP/scripts/readmanifest.pm $NIX_BIN_DIR/nix/

mkdir -p "$NIX_STATE_DIR"/manifests
mkdir -p "$NIX_STATE_DIR"/gcroots
mkdir -p "$NIX_STATE_DIR"/temproots
mkdir -p "$NIX_STATE_DIR"/profiles
ln -s "$NIX_STATE_DIR"/profiles "$NIX_STATE_DIR"/gcroots/

cat > "$NIX_CONF_DIR"/nix.conf <<EOF
gc-keep-outputs = false
gc-keep-derivations = false
env-keep-derivations = false
EOF

mkdir $NIX_DATA_DIR/nix
cp -pr $TOP/corepkgs $NIX_DATA_DIR/nix/
# Bah, script has the prefix hard-coded.  This is really messy stuff
# (and likely to fail).
for i in \
    $NIX_DATA_DIR/nix/corepkgs/nar/nar.sh \
    $NIX_BIN_DIR/nix/download-using-manifests.pl \
    $NIX_BIN_DIR/nix-prefetch-url \
    $NIX_BIN_DIR/nix-collect-garbage \
    $NIX_BIN_DIR/nix-build \
    $NIX_BIN_DIR/nix-install-package \
    $NIX_BIN_DIR/nix-push \
    $NIX_BIN_DIR/nix-pull \
    $NIX_BIN_DIR/nix/readmanifest.pm \
    ; do
    sed < $i > $i.tmp \
        -e "s^$REAL_BIN_DIR^$NIX_BIN_DIR^" \
        -e "s^$REAL_LIBEXEC_DIR^$NIX_LIBEXEC_DIR^" \
        -e "s^$REAL_LOCALSTATE_DIR^$NIX_LOCALSTATE_DIR^" \
        -e "s^$REAL_DATA_DIR^$NIX_DATA_DIR^" \
        -e "s^$REAL_STORE_DIR\([^/]\)^$NIX_STORE_DIR\1^"
    mv $i.tmp $i
    chmod +x $i
done

# Another ugly hack.
sed "s|^$|PATH='$PATH'|" < $NIX_DATA_DIR/nix/corepkgs/nar/nar.sh > tmp
chmod +x tmp
mv tmp $NIX_DATA_DIR/nix/corepkgs/nar/nar.sh

# Initialise the database.
$nixstore --init

# Did anything happen?
test -e "$NIX_DB_DIR"/validpaths
