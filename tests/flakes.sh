source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping flake tests"
    exit 99
fi

if [[ -z $(type -p hg) ]]; then
    echo "Mercurial not installed; skipping flake tests"
    exit 99
fi

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config

registry=$TEST_ROOT/registry.json

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2
flake3Dir=$TEST_ROOT/flake3
flake5Dir=$TEST_ROOT/flake5
flake6Dir=$TEST_ROOT/flake6
flake7Dir=$TEST_ROOT/flake7
templatesDir=$TEST_ROOT/templates
nonFlakeDir=$TEST_ROOT/nonFlake
flakeA=$TEST_ROOT/flakeA
flakeB=$TEST_ROOT/flakeB
flakeGitBare=$TEST_ROOT/flakeGitBare
flakeFollowsA=$TEST_ROOT/follows/flakeA
flakeFollowsB=$TEST_ROOT/follows/flakeA/flakeB
flakeFollowsC=$TEST_ROOT/follows/flakeA/flakeB/flakeC
flakeFollowsD=$TEST_ROOT/follows/flakeA/flakeD
flakeFollowsE=$TEST_ROOT/follows/flakeA/flakeE

for repo in $flake1Dir $flake2Dir $flake3Dir $flake7Dir $templatesDir $nonFlakeDir $flakeA $flakeB $flakeFollowsA; do
    rm -rf $repo $repo.tmp
    mkdir -p $repo
    git -C $repo init
    git -C $repo config user.email "foobar@example.com"
    git -C $repo config user.name "Foobar"
done

cat > $flake1Dir/flake.nix <<EOF
{
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

# Construct a custom registry, additionally test the --registry flag
nix registry add --registry $registry flake1 git+file://$flake1Dir
nix registry add --registry $registry flake2 git+file://$flake2Dir
nix registry add --registry $registry flake3 git+file://$flake3Dir
nix registry add --registry $registry flake4 flake3
nix registry add --registry $registry flake5 hg+file://$flake5Dir
nix registry add --registry $registry nixpkgs flake1
nix registry add --registry $registry templates git+file://$templatesDir

# Test 'nix flake list'.
[[ $(nix registry list | wc -l) == 7 ]]

# Test 'nix flake metadata'.
nix flake metadata flake1
nix flake metadata flake1 | grep -q 'Locked URL:.*flake1.*'

# Test 'nix flake metadata' on a local flake.
(cd $flake1Dir && nix flake metadata) | grep -q 'URL:.*flake1.*'
(cd $flake1Dir && nix flake metadata .) | grep -q 'URL:.*flake1.*'
nix flake metadata $flake1Dir | grep -q 'URL:.*flake1.*'

# Test 'nix flake metadata --json'.
json=$(nix flake metadata flake1 --json | jq .)
[[ $(echo "$json" | jq -r .description) = 'Bla bla' ]]
[[ -d $(echo "$json" | jq -r .path) ]]
[[ $(echo "$json" | jq -r .lastModified) = $(git -C $flake1Dir log -n1 --format=%ct) ]]
hash1=$(echo "$json" | jq -r .revision)

echo -n '# foo' >> $flake1Dir/flake.nix
git -C $flake1Dir commit -a -m 'Foo'
hash2=$(nix flake metadata flake1 --json --refresh | jq -r .revision)
[[ $hash1 != $hash2 ]]

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

# 'getFlake' on a mutable flakeref should fail in pure mode, but succeed in impure mode.
(! nix build -o $TEST_ROOT/result --expr "(builtins.getFlake \"$flake1Dir\").defaultPackage.$system")
nix build -o $TEST_ROOT/result --expr "(builtins.getFlake \"$flake1Dir\").defaultPackage.$system" --impure

# 'getFlake' on an immutable flakeref should succeed even in pure mode.
nix build -o $TEST_ROOT/result --expr "(builtins.getFlake \"git+file://$flake1Dir?rev=$hash2\").defaultPackage.$system"

# Building a flake with an unlocked dependency should fail in pure mode.
(! nix build -o $TEST_ROOT/result flake2#bar --no-registries)
(! nix build -o $TEST_ROOT/result flake2#bar --no-use-registries)
(! nix eval --expr "builtins.getFlake \"$flake2Dir\"")

# But should succeed in impure mode.
(! nix build -o $TEST_ROOT/result flake2#bar --impure)
nix build -o $TEST_ROOT/result flake2#bar --impure --no-write-lock-file

# Building a local flake with an unlocked dependency should fail with --no-update-lock-file.
nix build -o $TEST_ROOT/result $flake2Dir#bar --no-update-lock-file 2>&1 | grep 'requires lock file changes'

# But it should succeed without that flag.
nix build -o $TEST_ROOT/result $flake2Dir#bar --no-write-lock-file
nix build -o $TEST_ROOT/result $flake2Dir#bar --no-update-lock-file 2>&1 | grep 'requires lock file changes'
nix build -o $TEST_ROOT/result $flake2Dir#bar --commit-lock-file
[[ -e $flake2Dir/flake.lock ]]
[[ -z $(git -C $flake2Dir diff master) ]]

# Rerunning the build should not change the lockfile.
nix build -o $TEST_ROOT/result $flake2Dir#bar
[[ -z $(git -C $flake2Dir diff master) ]]

# Building with a lockfile should not require a fetch of the registry.
nix build -o $TEST_ROOT/result --flake-registry file:///no-registry.json $flake2Dir#bar --refresh
nix build -o $TEST_ROOT/result --no-registries $flake2Dir#bar --refresh
nix build -o $TEST_ROOT/result --no-use-registries $flake2Dir#bar --refresh

# Updating the flake should not change the lockfile.
nix flake lock $flake2Dir
[[ -z $(git -C $flake2Dir diff master) ]]

# Now we should be able to build the flake in pure mode.
nix build -o $TEST_ROOT/result flake2#bar

# Or without a registry.
nix build -o $TEST_ROOT/result --no-registries git+file://$flake2Dir#bar --refresh
nix build -o $TEST_ROOT/result --no-use-registries git+file://$flake2Dir#bar --refresh

# Test whether indirect dependencies work.
nix build -o $TEST_ROOT/result $flake3Dir#xyzzy
git -C $flake3Dir add flake.lock

# Add dependency to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
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

# Test whether registry caching works.
nix registry list --flake-registry file://$registry | grep -q flake3
mv $registry $registry.tmp
nix store gc
nix registry list --flake-registry file://$registry --refresh | grep -q flake3
mv $registry.tmp $registry

# Test whether flakes are registered as GC roots for offline use.
# FIXME: use tarballs rather than git.
rm -rf $TEST_HOME/.cache
nix store gc # get rid of copies in the store to ensure they get fetched to our git cache
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar
mv $flake1Dir $flake1Dir.tmp
mv $flake2Dir $flake2Dir.tmp
nix store gc
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar --refresh
mv $flake1Dir.tmp $flake1Dir
mv $flake2Dir.tmp $flake2Dir

# Add nonFlakeInputs to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
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
nix build -o $TEST_ROOT/result $flake3Dir#sth --commit-lock-file

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
nix flake lock $flake3Dir
[[ -z $(git -C $flake3Dir diff master) ]]

nix flake update $flake3Dir --override-flake flake2 nixpkgs
[[ ! -z $(git -C $flake3Dir diff master) ]]

# Make branch "removeXyzzy" where flake3 doesn't have xyzzy anymore
git -C $flake3Dir checkout -b removeXyzzy
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
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
nix flake lock $flake3Dir
git -C $flake3Dir add flake.nix flake.lock
git -C $flake3Dir commit -m 'Remove packages.xyzzy'
git -C $flake3Dir checkout master

# Test whether fuzzy-matching works for registry entries.
(! nix build -o $TEST_ROOT/result flake4/removeXyzzy#xyzzy)
nix build -o $TEST_ROOT/result flake4/removeXyzzy#sth

# Testing the nix CLI
nix registry add flake1 flake3
[[ $(nix registry list | wc -l) == 8 ]]
nix registry pin flake1
[[ $(nix registry list | wc -l) == 8 ]]
nix registry pin flake1 flake3
[[ $(nix registry list | wc -l) == 8 ]]
nix registry remove flake1
[[ $(nix registry list | wc -l) == 7 ]]

# Test 'nix flake init'.
cat > $templatesDir/flake.nix <<EOF
{
  description = "Some templates";

  outputs = { self }: {
    templates = {
      trivial = {
        path = ./trivial;
        description = "A trivial flake";
      };
    };
    defaultTemplate = self.templates.trivial;
  };
}
EOF

mkdir $templatesDir/trivial

cat > $templatesDir/trivial/flake.nix <<EOF
{
  description = "A flake for building Hello World";

  outputs = { self, nixpkgs }: {
    packages.x86_64-linux.hello = nixpkgs.legacyPackages.x86_64-linux.hello;
    defaultPackage.x86_64-linux = self.packages.x86_64-linux.hello;
  };
}
EOF

git -C $templatesDir add flake.nix trivial/flake.nix
git -C $templatesDir commit -m 'Initial'

nix flake check templates
nix flake show templates

(cd $flake7Dir && nix flake init)
(cd $flake7Dir && nix flake init) # check idempotence
git -C $flake7Dir add flake.nix
nix flake check $flake7Dir
nix flake show $flake7Dir
git -C $flake7Dir commit -a -m 'Initial'

# Test 'nix flake new'.
rm -rf $flake6Dir
nix flake new -t templates#trivial $flake6Dir
nix flake new -t templates#trivial $flake6Dir # check idempotence
nix flake check $flake6Dir

# Test 'nix flake clone'.
rm -rf $TEST_ROOT/flake1-v2
nix flake clone flake1 --dest $TEST_ROOT/flake1-v2
[ -e $TEST_ROOT/flake1-v2/flake.nix ]

# More 'nix flake check' tests.
cat > $flake3Dir/flake.nix <<EOF
{
  outputs = { flake1, self }: {
    overlay = final: prev: {
    };
  };
}
EOF

nix flake check $flake3Dir

cat > $flake3Dir/flake.nix <<EOF
{
  outputs = { flake1, self }: {
    overlay = finalll: prev: {
    };
  };
}
EOF

(! nix flake check $flake3Dir)

cat > $flake3Dir/flake.nix <<EOF
{
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
  outputs = { flake1, self }: {
    nixosModule = { config, pkgs }: {
      a.b.c = 123;
    };
  };
}
EOF

(! nix flake check $flake3Dir)

cat > $flake3Dir/flake.nix <<EOF
{
  outputs = { flake1, self }: {
    defaultPackage = {
        system-1 = "foo";
        system-2 = "bar";
    };
  };
}
EOF

checkRes=$(nix flake check --keep-going $flake3Dir 2>&1 && fail "nix flake check should have failed" || true)
echo "$checkRes" | grep -q "defaultPackage.system-1"
echo "$checkRes" | grep -q "defaultPackage.system-2"

# Test 'follows' inputs.
cat > $flake3Dir/flake.nix <<EOF
{
  inputs.foo = {
    type = "indirect";
    id = "flake1";
  };
  inputs.bar.follows = "foo";

  outputs = { self, foo, bar }: {
  };
}
EOF

nix flake lock $flake3Dir
[[ $(jq -c .nodes.root.inputs.bar $flake3Dir/flake.lock) = '["foo"]' ]]

cat > $flake3Dir/flake.nix <<EOF
{
  inputs.bar.follows = "flake2/flake1";

  outputs = { self, flake2, bar }: {
  };
}
EOF

nix flake lock $flake3Dir
[[ $(jq -c .nodes.root.inputs.bar $flake3Dir/flake.lock) = '["flake2","flake1"]' ]]

cat > $flake3Dir/flake.nix <<EOF
{
  inputs.bar.follows = "flake2";

  outputs = { self, flake2, bar }: {
  };
}
EOF

nix flake lock $flake3Dir
[[ $(jq -c .nodes.root.inputs.bar $flake3Dir/flake.lock) = '["flake2"]' ]]

# Test overriding inputs of inputs.
cat > $flake3Dir/flake.nix <<EOF
{
  inputs.flake2.inputs.flake1 = {
    type = "git";
    url = file://$flake7Dir;
  };

  outputs = { self, flake2 }: {
  };
}
EOF

nix flake lock $flake3Dir
[[ $(jq .nodes.flake1.locked.url $flake3Dir/flake.lock) =~ flake7 ]]

cat > $flake3Dir/flake.nix <<EOF
{
  inputs.flake2.inputs.flake1.follows = "foo";
  inputs.foo.url = git+file://$flake7Dir;

  outputs = { self, flake2 }: {
  };
}
EOF

nix flake update $flake3Dir
[[ $(jq -c .nodes.flake2.inputs.flake1 $flake3Dir/flake.lock) =~ '["foo"]' ]]
[[ $(jq .nodes.foo.locked.url $flake3Dir/flake.lock) =~ flake7 ]]

# Test git+file with bare repo.
rm -rf $flakeGitBare
git clone --bare $flake1Dir $flakeGitBare
nix build -o $TEST_ROOT/result git+file://$flakeGitBare

# Test Mercurial flakes.
rm -rf $flake5Dir
hg init $flake5Dir

cat > $flake5Dir/flake.nix <<EOF
{
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

(! nix flake metadata --json hg+file://$flake5Dir | jq -e -r .revision)

nix eval hg+file://$flake5Dir#expr

nix eval hg+file://$flake5Dir#expr

(! nix eval hg+file://$flake5Dir#expr --no-allow-dirty)

(! nix flake metadata --json hg+file://$flake5Dir | jq -e -r .revision)

hg commit --config ui.username=foobar@example.org $flake5Dir -m 'Add lock file'

nix flake metadata --json hg+file://$flake5Dir --refresh | jq -e -r .revision
nix flake metadata --json hg+file://$flake5Dir
[[ $(nix flake metadata --json hg+file://$flake5Dir | jq -e -r .revCount) = 1 ]]

nix build -o $TEST_ROOT/result hg+file://$flake5Dir --no-registries --no-allow-dirty
nix build -o $TEST_ROOT/result hg+file://$flake5Dir --no-use-registries --no-allow-dirty

# Test tarball flakes
tar cfz $TEST_ROOT/flake.tar.gz -C $TEST_ROOT --exclude .hg flake5

nix build -o $TEST_ROOT/result file://$TEST_ROOT/flake.tar.gz

# Building with a tarball URL containing a SRI hash should also work.
url=$(nix flake metadata --json file://$TEST_ROOT/flake.tar.gz | jq -r .url)
[[ $url =~ sha256- ]]

nix build -o $TEST_ROOT/result $url

# Building with an incorrect SRI hash should fail.
nix build -o $TEST_ROOT/result "file://$TEST_ROOT/flake.tar.gz?narHash=sha256-qQ2Zz4DNHViCUrp6gTS7EE4+RMqFQtUfWF2UNUtJKS0=" 2>&1 | grep 'NAR hash mismatch'

# Test --override-input.
git -C $flake3Dir reset --hard
nix flake lock $flake3Dir --override-input flake2/flake1 flake5 -vvvvv
[[ $(jq .nodes.flake1_2.locked.url $flake3Dir/flake.lock) =~ flake5 ]]

nix flake lock $flake3Dir --override-input flake2/flake1 flake1
[[ $(jq -r .nodes.flake1_2.locked.rev $flake3Dir/flake.lock) =~ $hash2 ]]

nix flake lock $flake3Dir --override-input flake2/flake1 flake1/master/$hash1
[[ $(jq -r .nodes.flake1_2.locked.rev $flake3Dir/flake.lock) =~ $hash1 ]]

# Test --update-input.
nix flake lock $flake3Dir
[[ $(jq -r .nodes.flake1_2.locked.rev $flake3Dir/flake.lock) = $hash1 ]]

nix flake lock $flake3Dir --update-input flake2/flake1
[[ $(jq -r .nodes.flake1_2.locked.rev $flake3Dir/flake.lock) =~ $hash2 ]]

# Test 'nix flake metadata --json'.
nix flake metadata $flake3Dir --json | jq .

# Test circular flake dependencies.
cat > $flakeA/flake.nix <<EOF
{
  inputs.b.url = git+file://$flakeB;
  inputs.b.inputs.a.follows = "/";

  outputs = { self, nixpkgs, b }: {
    foo = 123 + b.bar;
    xyzzy = 1000;
  };
}
EOF

git -C $flakeA add flake.nix

cat > $flakeB/flake.nix <<EOF
{
  inputs.a.url = git+file://$flakeA;

  outputs = { self, nixpkgs, a }: {
    bar = 456 + a.xyzzy;
  };
}
EOF

git -C $flakeB add flake.nix
git -C $flakeB commit -a -m 'Foo'

[[ $(nix eval $flakeA#foo) = 1579 ]]
[[ $(nix eval $flakeA#foo) = 1579 ]]

sed -i $flakeB/flake.nix -e 's/456/789/'
git -C $flakeB commit -a -m 'Foo'

[[ $(nix eval --update-input b $flakeA#foo) = 1912 ]]

# Test list-inputs with circular dependencies
nix flake metadata $flakeA

# Test flake follow paths
mkdir -p $flakeFollowsB
mkdir -p $flakeFollowsC
mkdir -p $flakeFollowsD
mkdir -p $flakeFollowsE

cat > $flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        B = {
            url = "path:./flakeB";
            inputs.foobar.follows = "D";
        };

        D.url = "path:./flakeD";
        foobar.url = "path:./flakeE";
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsB/flake.nix <<EOF
{
    description = "Flake B";
    inputs = {
        foobar.url = "path:./../flakeE";
        C = {
            url = "path:./flakeC";
            inputs.foobar.follows = "foobar";
        };
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsC/flake.nix <<EOF
{
    description = "Flake C";
    inputs = {
        foobar.url = "path:./../../flakeE";
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsD/flake.nix <<EOF
{
    description = "Flake D";
    inputs = {};
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsE/flake.nix <<EOF
{
    description = "Flake D";
    inputs = {};
    outputs = { ... }: {};
}
EOF

git -C $flakeFollowsA add flake.nix flakeB/flake.nix \
  flakeB/flakeC/flake.nix flakeD/flake.nix flakeE/flake.nix

nix flake lock $flakeFollowsA

[[ $(jq -c .nodes.B.inputs.C $flakeFollowsA/flake.lock) = '"C"' ]]
[[ $(jq -c .nodes.B.inputs.foobar $flakeFollowsA/flake.lock) = '["D"]' ]]
[[ $(jq -c .nodes.C.inputs.foobar $flakeFollowsA/flake.lock) = '["B","foobar"]' ]]

# Ensure a relative path is not allowed to go outside the store path
cat > $flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        B.url = "path:./../../flakeB";
    };
    outputs = { ... }: {};
}
EOF

git -C $flakeFollowsA add flake.nix

nix flake lock $flakeFollowsA 2>&1 | grep 'this is a security violation'
