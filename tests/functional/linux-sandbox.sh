source common.sh

needLocalStore "the sandbox only runs on the builder side, so it makes no sense to test it with the daemon"

clearStore

requireSandboxSupport

# Note: we need to bind-mount $SHELL into the chroot. Currently we
# only support the case where $SHELL is in the Nix store, because
# otherwise things get complicated (e.g. if it's in /bin, do we need
# /lib as well?).
if [[ ! $SHELL =~ /nix/store ]]; then skipTest "Shell is not from Nix store"; fi
# An alias to automatically bind-mount the $SHELL on nix-build invocations
nix-sandbox-build () { nix-build --no-out-link --sandbox-paths /nix/store "$@"; }

chmod -R u+w $TEST_ROOT/store0 || true
rm -rf $TEST_ROOT/store0

export NIX_STORE_DIR=/my/store
export NIX_REMOTE=$TEST_ROOT/store0

outPath=$(nix-sandbox-build dependencies.nix)

[[ $outPath =~ /my/store/.*-dependencies ]]

nix path-info -r $outPath | grep input-2

nix store ls -R -l $outPath | grep foobar

nix store cat $outPath/foobar | grep FOOBAR

# Test --check without hash rewriting.
nix-sandbox-build dependencies.nix --check

# Test that sandboxed builds with --check and -K can move .check directory to store
nix-sandbox-build check.nix -A nondeterministic

# `100 + 4` means non-determinstic, see doc/manual/src/command-ref/status-build-failure.md
expectStderr 104 nix-sandbox-build check.nix -A nondeterministic --check -K > $TEST_ROOT/log
grepQuietInverse 'error: renaming' $TEST_ROOT/log
grepQuiet 'may not be deterministic' $TEST_ROOT/log

# Test that sandboxed builds cannot write to /etc easily
# `100` means build failure without extra info, see doc/manual/src/command-ref/status-build-failure.md
expectStderr 100 nix-sandbox-build -E 'with import ./config.nix; mkDerivation { name = "etc-write"; buildCommand = "echo > /etc/test"; }' |
    grepQuiet "/etc/test: Permission denied"


## Test mounting of SSL certificates into the sandbox
testCert () {
    expectation=$1 # "missing" | "present"
    mode=$2        # "normal" | "fixed-output"
    certFile=$3    # a string that can be the path to a cert file
    # `100` means build failure without extra info, see doc/manual/src/command-ref/status-build-failure.md
    [ "$mode" == fixed-output ] && ret=1 || ret=100
    expectStderr $ret nix-sandbox-build linux-sandbox-cert-test.nix --argstr mode "$mode" --option ssl-cert-file "$certFile" |
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

# Symlinks should be added in the sandbox directly and not followed
nix-sandbox-build symlink-derivation.nix
