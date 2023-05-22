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

expectStderr 104 nix-build check.nix -A nondeterministic --sandbox-paths /nix/store --no-out-link --check -K \
  | tee >( grepQuietInverse 'error: renaming' ) \
  | grepQuiet 'may not be deterministic'

# Test that sandboxed builds cannot write to /etc easily
expect 100 nix-build -E 'with import ./config.nix; mkDerivation { name = "etc-write"; buildCommand = "echo > /etc/test"; }' --no-out-link --sandbox-paths /nix/store


## Test mounting of SSL certificates into the sandbox
testCert () {
    expectation=$1 # "missing" | "present"
    mode=$2        # "normal" | "fixed-output"
    certFile=$3    # a string that can be the path to a cert file
    [ "$mode" == fixed-output ] && ret=1 || ret=100
    expectStderr $ret nix-build linux-sandbox-cert-test.nix --argstr mode "$mode" --no-out-link --sandbox-paths /nix/store --option ssl-cert-file "$certFile" | \
      # tee /dev/stderr | \
      grepQuiet "CERT_${expectation}_IN_SANDBOX"
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
