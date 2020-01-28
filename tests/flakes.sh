source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping flake tests"
    exit 99
fi

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config

registry=$TEST_ROOT/registry.json

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2
flake3Dir=$TEST_ROOT/flake3
flake4Dir=$TEST_ROOT/flake4
flake5Dir=$TEST_ROOT/flake5
flake7Dir=$TEST_ROOT/flake7
nonFlakeDir=$TEST_ROOT/nonFlake

for repo in $flake1Dir $flake2Dir $flake3Dir $flake7Dir $nonFlakeDir; do
    rm -rf $repo $repo.tmp
    mkdir $repo
    git -C $repo init
    git -C $repo config user.email "foobar@example.com"
    git -C $repo config user.name "Foobar"
done

cat > $flake1Dir/flake.nix <<EOF
{
  edition = 201909;

  description = "Bla bla";

  outputs = inputs: rec {
    packages.$system.foo = import ./simple.nix;
    defaultPackage.$system = packages.$system.foo;

    # To test "nix flake init".
    legacyPackages.x86_64-linux.hello = import ./simple.nix;
  };
}
EOF

cp ./simple.nix ./simple.builder.sh ./config.nix $flake1Dir/
git -C $flake1Dir add flake.nix simple.nix simple.builder.sh config.nix
git -C $flake1Dir commit -m 'Initial'

cat > $flake2Dir/flake.nix <<EOF
{
  edition = 201909;

  description = "Fnord";

  outputs = { self, flake1 }: rec {
    packages.$system.bar = flake1.packages.$system.foo;
  };
}
EOF

git -C $flake2Dir add flake.nix
git -C $flake2Dir commit -m 'Initial'

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  description = "Fnord";

  outputs = { self, flake2 }: rec {
    packages.$system.xyzzy = flake2.packages.$system.bar;

    checks = {
      xyzzy = packages.$system.xyzzy;
    };
  };
}
EOF

git -C $flake3Dir add flake.nix
git -C $flake3Dir commit -m 'Initial'

cat > $nonFlakeDir/README.md <<EOF
FNORD
EOF

git -C $nonFlakeDir add README.md
git -C $nonFlakeDir commit -m 'Initial'

cat > $registry <<EOF
{
    "flakes": {
        "flake:flake1": {
            "url": "git+file://$flake1Dir"
        },
        "flake:flake2": {
            "url": "git+file://$flake2Dir"
        },
        "flake:flake3": {
            "url": "git+file://$flake3Dir"
        },
        "flake:flake4": {
            "url": "flake:flake3"
        },
        "flake:flake5": {
            "url": "hg+file://$flake5Dir"
        },
        "flake:nixpkgs": {
            "url": "flake:flake1"
        }
    },
    "version": 1
}
EOF

# Test 'nix flake list'.
(( $(nix flake list | wc -l) == 6 ))

# Test 'nix flake info'.
nix flake info flake1 | grep -q 'URL: .*flake1.*'

# Test 'nix flake info' on a local flake.
(cd $flake1Dir && nix flake info) | grep -q 'URL: .*flake1.*'
(cd $flake1Dir && nix flake info .) | grep -q 'URL: .*flake1.*'
nix flake info $flake1Dir | grep -q 'URL: .*flake1.*'

# Test 'nix flake info --json'.
json=$(nix flake info flake1 --json | jq .)
[[ $(echo "$json" | jq -r .description) = 'Bla bla' ]]
[[ -d $(echo "$json" | jq -r .path) ]]
[[ $(echo "$json" | jq -r .lastModified) = $(git -C $flake1Dir log -n1 --format=%ct) ]]

# Test 'nix build' on a flake.
nix build -o $TEST_ROOT/result flake1#foo
[[ -e $TEST_ROOT/result/hello ]]

# Test defaultPackage.
nix build -o $TEST_ROOT/result flake1
[[ -e $TEST_ROOT/result/hello ]]

nix build -o $TEST_ROOT/result $flake1Dir
nix build -o $TEST_ROOT/result git+file://$flake1Dir

# Check that store symlinks inside a flake are not interpreted as flakes.
nix build -o $flake1Dir/result git+file://$flake1Dir
nix path-info $flake1Dir/result

# Building a flake with an unlocked dependency should fail in pure mode.
(! nix build -o $TEST_ROOT/result flake2#bar --no-registries)
(! nix eval --expr "builtins.getFlake \"$flake2Dir\"")

# But should succeed in impure mode.
nix build -o $TEST_ROOT/result flake2#bar --impure

# Test automatic lock file generation.
nix build -o $TEST_ROOT/result $flake2Dir#bar
[[ -e $flake2Dir/flake.lock ]]
git -C $flake2Dir add flake.lock
git -C $flake2Dir commit flake.lock -m 'Add flake.lock'

# Rerunning the build should not change the lockfile.
nix build -o $TEST_ROOT/result $flake2Dir#bar
[[ -z $(git -C $flake2Dir diff master) ]]

# Building with a lockfile should not require a fetch of the registry.
nix build -o $TEST_ROOT/result --flake-registry file:///no-registry.json $flake2Dir#bar --tarball-ttl 0
nix build -o $TEST_ROOT/result --no-registries $flake2Dir#bar --tarball-ttl 0

# Updating the flake should not change the lockfile.
nix flake update $flake2Dir
[[ -z $(git -C $flake2Dir diff master) ]]

# Now we should be able to build the flake in pure mode.
nix build -o $TEST_ROOT/result flake2#bar

# Or without a registry.
nix build -o $TEST_ROOT/result --no-registries git+file://$flake2Dir#bar --tarball-ttl 0

# Test whether indirect dependencies work.
nix build -o $TEST_ROOT/result $flake3Dir#xyzzy
git -C $flake3Dir add flake.lock

# Add dependency to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  description = "Fnord";

  outputs = { self, flake1, flake2 }: rec {
    packages.$system.xyzzy = flake2.packages.$system.bar;
    packages.$system."sth sth" = flake1.packages.$system.foo;
  };
}
EOF

git -C $flake3Dir add flake.nix
git -C $flake3Dir commit -m 'Update flake.nix'

# Check whether `nix build` works with an incomplete lockfile
nix build -o $TEST_ROOT/result $flake3Dir#"sth sth"
nix build -o $TEST_ROOT/result $flake3Dir#"sth%20sth"

# Check whether it saved the lockfile
(! [[ -z $(git -C $flake3Dir diff master) ]])

git -C $flake3Dir add flake.lock

git -C $flake3Dir commit -m 'Add lockfile'

# Unsupported editions should be an error.
sed -i $flake3Dir/flake.nix -e s/201909/201912/
nix build -o $TEST_ROOT/result $flake3Dir#sth 2>&1 | grep 'unsupported edition'

# Test whether registry caching works.
nix flake list --flake-registry file://$registry | grep -q flake3
mv $registry $registry.tmp
nix flake list --flake-registry file://$registry --tarball-ttl 0 | grep -q flake3
mv $registry.tmp $registry

# Test whether flakes are registered as GC roots for offline use.
# FIXME: use tarballs rather than git.
rm -rf $TEST_HOME/.cache
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar
mv $flake1Dir $flake1Dir.tmp
mv $flake2Dir $flake2Dir.tmp
nix-store --gc
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar --tarball-ttl 0
mv $flake1Dir.tmp $flake1Dir
mv $flake2Dir.tmp $flake2Dir

# Add nonFlakeInputs to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  inputs = {
    flake1 = {};
    flake2 = {};
    nonFlake = {
      url = git+file://$nonFlakeDir;
      flake = false;
    };
  };

  description = "Fnord";

  outputs = inputs: rec {
    packages.$system.xyzzy = inputs.flake2.packages.$system.bar;
    packages.$system.sth = inputs.flake1.packages.$system.foo;
    packages.$system.fnord =
      with import ./config.nix;
      mkDerivation {
        inherit system;
        name = "fnord";
        buildCommand = ''
          cat \${inputs.nonFlake}/README.md > \$out
        '';
      };
  };
}
EOF

cp ./config.nix $flake3Dir

git -C $flake3Dir add flake.nix config.nix
git -C $flake3Dir commit -m 'Add nonFlakeInputs'

# Check whether `nix build` works with a lockfile which is missing a
# nonFlakeInputs.
nix build -o $TEST_ROOT/result $flake3Dir#sth

git -C $flake3Dir add flake.lock

git -C $flake3Dir commit -m 'Update nonFlakeInputs'

nix build -o $TEST_ROOT/result flake3#fnord
[[ $(cat $TEST_ROOT/result) = FNORD ]]

# Check whether flake input fetching is lazy: flake3#sth does not
# depend on flake2, so this shouldn't fail.
rm -rf $TEST_HOME/.cache
clearStore
mv $flake2Dir $flake2Dir.tmp
mv $nonFlakeDir $nonFlakeDir.tmp
nix build -o $TEST_ROOT/result flake3#sth
(! nix build -o $TEST_ROOT/result flake3#xyzzy)
(! nix build -o $TEST_ROOT/result flake3#fnord)
mv $flake2Dir.tmp $flake2Dir
mv $nonFlakeDir.tmp $nonFlakeDir
nix build -o $TEST_ROOT/result flake3#xyzzy flake3#fnord

# Test doing multiple `lookupFlake`s
nix build -o $TEST_ROOT/result flake4#xyzzy

# Test 'nix flake update' and --override-flake.
nix flake update $flake3Dir
[[ -z $(git -C $flake3Dir diff master) ]]

nix flake update $flake3Dir --recreate-lock-file --override-flake flake2 nixpkgs
[[ ! -z $(git -C $flake3Dir diff master) ]]

# Make branch "removeXyzzy" where flake3 doesn't have xyzzy anymore
git -C $flake3Dir checkout -b removeXyzzy
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  inputs = {
    nonFlake = {
      url = "$nonFlakeDir";
      flake = false;
    };
  };

  description = "Fnord";

  outputs = { self, flake1, flake2, nonFlake }: rec {
    packages.$system.sth = flake1.packages.$system.foo;
    packages.$system.fnord =
      with import ./config.nix;
      mkDerivation {
        inherit system;
        name = "fnord";
        buildCommand = ''
          cat \${nonFlake}/README.md > \$out
        '';
      };
  };
}
EOF
git -C $flake3Dir add flake.nix
git -C $flake3Dir commit -m 'Remove packages.xyzzy'
git -C $flake3Dir checkout master

# Test whether fuzzy-matching works for IsAlias
(! nix build -o $TEST_ROOT/result flake4/removeXyzzy#xyzzy)

# Test whether fuzzy-matching works for IsGit
(! nix build -o $TEST_ROOT/result flake4/removeXyzzy#xyzzy)
nix build -o $TEST_ROOT/result flake4/removeXyzzy#sth

# Testing the nix CLI
nix flake add flake1 flake3
(( $(nix flake list | wc -l) == 7 ))
nix flake pin flake1
(( $(nix flake list | wc -l) == 7 ))
nix flake remove flake1
(( $(nix flake list | wc -l) == 6 ))

# Test 'nix flake init'.
(cd $flake7Dir && nix flake init)
git -C $flake7Dir add flake.nix
nix flake check $flake7Dir
git -C $flake7Dir commit -m 'Initial'

# Test 'nix flake clone'.
rm -rf $TEST_ROOT/flake1-v2
nix flake clone flake1 --dest $TEST_ROOT/flake1-v2
[ -e $TEST_ROOT/flake1-v2/flake.nix ]

# More 'nix flake check' tests.
cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  outputs = { flake1, self }: {
    overlay = final: prev: {
    };
  };
}
EOF

nix flake check $flake3Dir

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  outputs = { flake1, self }: {
    overlay = finalll: prev: {
    };
  };
}
EOF

(! nix flake check $flake3Dir)

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  outputs = { flake1, self }: {
    nixosModules.foo = {
      a.b.c = 123;
      foo = true;
    };
  };
}
EOF

nix flake check $flake3Dir

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  outputs = { flake1, self }: {
    nixosModules.foo = {
      a.b.c = 123;
      foo = assert false; true;
    };
  };
}
EOF

(! nix flake check $flake3Dir)

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  outputs = { flake1, self }: {
    nixosModule = { config, pkgs, ... }: {
      a.b.c = 123;
    };
  };
}
EOF

nix flake check $flake3Dir

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  outputs = { flake1, self }: {
    nixosModule = { config, pkgs }: {
      a.b.c = 123;
    };
  };
}
EOF

(! nix flake check $flake3Dir)

# Test 'follows' inputs.
cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  inputs.foo.url = flake:flake1;
  inputs.bar.follows = "foo";

  outputs = { self, foo, bar }: {
  };
}
EOF

nix flake update $flake3Dir
[[ $(jq .inputs.foo.url $flake3Dir/flake.lock) = $(jq .inputs.bar.url $flake3Dir/flake.lock) ]]

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  inputs.bar.follows = "flake2/flake1";

  outputs = { self, flake2, bar }: {
  };
}
EOF

nix flake update $flake3Dir
[[ $(jq .inputs.bar.url $flake3Dir/flake.lock) =~ flake1 ]]

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  inputs.bar.follows = "flake2";

  outputs = { self, flake2, bar }: {
  };
}
EOF

nix flake update $flake3Dir
[[ $(jq .inputs.bar.url $flake3Dir/flake.lock) =~ flake2 ]]

# Test overriding inputs of inputs.
cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  inputs.flake2.inputs.flake1.url = git+file://$flake7Dir;

  outputs = { self, flake2 }: {
  };
}
EOF

nix flake update $flake3Dir
[[ $(jq .inputs.flake2.inputs.flake1.url $flake3Dir/flake.lock) =~ flake7 ]]

cat > $flake3Dir/flake.nix <<EOF
{
  edition = 201909;

  inputs.flake2.inputs.flake1.follows = "foo";
  inputs.foo.url = git+file://$flake7Dir;

  outputs = { self, flake2 }: {
  };
}
EOF

nix flake update $flake3Dir --recreate-lock-file
[[ $(jq .inputs.flake2.inputs.flake1.url $flake3Dir/flake.lock) =~ flake7 ]]

# Test Mercurial flakes.
if [[ -z $(type -p hg) ]]; then
    echo "Git not installed; skipping Mercurial flake tests"
    exit 99
fi

rm -rf $flake5Dir
hg init $flake5Dir

cat > $flake5Dir/flake.nix <<EOF
{
  edition = 201909;

  outputs = { self, flake1 }: {
    defaultPackage.$system = flake1.defaultPackage.$system;

    expr = assert builtins.pathExists ./flake.lock; 123;
  };
}
EOF

hg add $flake5Dir/flake.nix
hg commit --config ui.username=foobar@example.org $flake5Dir -m 'Initial commit'

nix build -o $TEST_ROOT/result hg+file://$flake5Dir
[[ -e $TEST_ROOT/result/hello ]]

nix flake info --json hg+file://$flake5Dir | jq -e -r .revision

# This will fail because flake.lock is not tracked by Mercurial.
(! nix eval hg+file://$flake5Dir#expr)

hg add $flake5Dir/flake.lock

nix eval hg+file://$flake5Dir#expr

(! nix eval hg+file://$flake5Dir#expr --no-allow-dirty)

(! nix flake info --json hg+file://$flake5Dir | jq -e -r .revision)

hg commit --config ui.username=foobar@example.org $flake5Dir -m 'Add lock file'

nix flake info --json hg+file://$flake5Dir --refresh | jq -e -r .revision
nix flake info --json hg+file://$flake5Dir
[[ $(nix flake info --json hg+file://$flake5Dir | jq -e -r .revCount) = 1 ]]

nix build -o $TEST_ROOT/result hg+file://$flake5Dir --no-registries --no-allow-dirty

# Test tarball flakes
rm -rf $flake5Dir/.hg
tar cfz $TEST_ROOT/flake.tar.gz -C $TEST_ROOT flake5

nix build -o $TEST_ROOT/result file://$TEST_ROOT/flake.tar.gz

# Building with a tarball URL containing a SRI hash should also work.
url=$(nix flake info --json file://$TEST_ROOT/flake.tar.gz | jq -r .url)
[[ $url =~ sha256- ]]

nix build -o $TEST_ROOT/result $url

# Building with an incorrect SRI hash should fail.
nix build -o $TEST_ROOT/result "file://$TEST_ROOT/flake.tar.gz?narHash=sha256-qQ2Zz4DNHViCUrp6gTS7EE4+RMqFQtUfWF2UNUtJKS0=" 2>&1 | grep 'NAR hash mismatch'
