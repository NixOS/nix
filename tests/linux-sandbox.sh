source common.sh

needLocalStore "the sandbox only runs on the builder side, so it makes no sense to test it with the daemon"

clearStore

requireSandboxSupport

# Note: we need to bind-mount $SHELL into the chroot. Currently we
# only support the case where $SHELL is in the Nix store, because
# otherwise things get complicated (e.g. if it's in /bin, do we need
# /lib as well?).
if [[ ! $SHELL =~ /nix/store ]]; then skipTest "Shell is not from Nix store"; fi

chmod -R u+w $TEST_ROOT/store0 || true
rm -rf $TEST_ROOT/store0

export NIX_STORE_DIR=/my/store
export NIX_REMOTE=$TEST_ROOT/store0

outPath=$(nix-build dependencies.nix --no-out-link --sandbox-paths /nix/store)

[[ $outPath =~ /my/store/.*-dependencies ]]

nix path-info -r $outPath | grep input-2

nix store ls -R -l $outPath | grep foobar

nix store cat $outPath/foobar | grep FOOBAR

# Test --check without hash rewriting.
nix-build dependencies.nix --no-out-link --check --sandbox-paths /nix/store

# Test that sandboxed builds with --check and -K can move .check directory to store
nix-build check.nix -A nondeterministic --sandbox-paths /nix/store --no-out-link

(! nix-build check.nix -A nondeterministic --sandbox-paths /nix/store --no-out-link --check -K 2> $TEST_ROOT/log)
if grepQuiet 'error: renaming' $TEST_ROOT/log; then false; fi
grepQuiet 'may not be deterministic' $TEST_ROOT/log

# Test that sandboxed builds cannot write to /etc easily
(! nix-build -E 'with import ./config.nix; mkDerivation { name = "etc-write"; buildCommand = "echo > /etc/test"; }' --no-out-link --sandbox-paths /nix/store)


## Test mounting of SSL certificates into the sandbox
testCert () {
    (! nix-build linux-sandbox-cert-test.nix --argstr fixed-output "$2" --no-out-link --sandbox-paths /nix/store --option ssl-cert-file "$3" 2> $TEST_ROOT/log)
    cat $TEST_ROOT/log
    grepQuiet "CERT_${1}_IN_SANDBOX" $TEST_ROOT/log
}

nocert=$TEST_ROOT/no-cert-file.pem
cert=$TEST_ROOT/some-cert-file.pem
echo -n "CERT_CONTENT" > $cert

# No cert in sandbox when not a fixed-output derivation
testCert missing normal       "$cert"

# No cert in sandbox when ssl-cert-file is empty
testCert missing fixed-output ""

# No cert in sandbox when ssl-cert-file is a nonexistent file
testCert missing fixed-output "$nocert"

# Cert in sandbox when ssl-cert-file is set to an existing file
testCert present fixed-output "$cert"
