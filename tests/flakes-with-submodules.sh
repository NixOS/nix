source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping flake tests"
    exit 99
fi

clearStore
rm -rf $TEST_HOME/.{cache,config}

registry=$TEST_ROOT/registry.json

nonFlakeDir=$TEST_ROOT/nonFlake
flakeDir=$TEST_ROOT/flake
flakeWithSubmodules=$TEST_ROOT/flakeWithSubmodules

for repo in $flakeDir $nonFlakeDir $flakeWithSubmodules; do
    rm -rf $repo $repo.tmp
    mkdir -p $repo
    git -C $repo init
    git -C $repo config user.email "foobar@example.com"
    git -C $repo config user.name "Foobar"
done

for dir in $nonFlakeDir $flakeDir; do
cat > $dir/README.md <<EOF
FNORD
EOF
git -C $dir add README.md
git -C $dir commit -m 'Initial'
done

cp config.nix $flakeDir

cat > $flakeDir/flake.nix <<EOF
{
    description = "Flake whose defaultPackage prints out its README.md";

    outputs = { self }: with import ./config.nix; let
        inherit (self.modules) nonFlake;
        writeShellScriptBin = name: script: mkDerivation {
            inherit name;
            buildCommand = ''
                mkdir -p \$out/bin
                bin=\$_/\${name}
                echo '#!/bin/sh' > \$bin
                echo '\${script}' >> \$bin
                chmod +x \$bin
            '';
        };
    in {
        packages.$system = {
            cat-own-readme = writeShellScriptBin "cat-own-readme" ''
                cat \${self}/README.md
            '';
        };

        defaultPackage.$system = self.packages.$system.cat-own-readme;
    };
}
EOF

git -C $flakeDir add .
git -C $flakeDir commit -m'add flake.nix'

[[ $(nix run $flakeDir#cat-own-readme) == "FNORD" ]]

git -C $flakeWithSubmodules submodule add $nonFlakeDir
git -C $flakeWithSubmodules submodule add $flakeDir
git -C $flakeWithSubmodules add .
git -C $flakeWithSubmodules commit -m'add submodules'

cp config.nix $flakeWithSubmodules

cat > $flakeWithSubmodules/flake.nix <<EOF
{
    description = "Flake with Submodules";

    outputs = { self }: with import ./config.nix; let
        traceVal = val: builtins.trace val val;
        nonFlake = builtins.fetchTree (traceVal self.modules.nonFlake);
        flake = builtins.getFlake (traceVal self.modules.flake);

        writeShellScriptBin = name: script: mkDerivation {
            inherit name;
            buildCommand = ''
                mkdir -p \$out/bin
                bin=\$_/\${name}
                echo '#!/bin/sh' > \$bin
                echo '\${script}' >> \$bin
                chmod +x \$bin
            '';
        };
    in {
        packages.$system = {
            cat-submodule-readme = writeShellScriptBin "cat-submodule-readme" ''
                cat \${nonFlake}/README.md
            '';
            use-submodule-as-flake = flake.packages.$system.cat-own-readme;
        };

        defaultPackage.$system = self.packages.$system.cat-submodule-readme;
    };
}
EOF

git -C $flakeWithSubmodules add .
git -C $flakeWithSubmodules commit -m'add flake.nix'

[[ $(nix run $flakeWithSubmodules#cat-submodule-readme) == "FNORD" ]]
[[ $(nix run $flakeWithSubmodules#use-submodule-as-flake) == "FNORD" ]]

# TODO make the dirty case do the Right Thing as well.
#echo FSOUTH > $flakeWithSubmodules/nonFlake/README.md
#[[ $(nix run $flakeWithSubmodules#cat-submodule-readme) == "FSOUTH" ]]
# [[ $(nix run $flakeWithSubmodules#use-submodule-as-flake) == "FSOUTH" ]]
# nix run -vv $flakeWithSubmodules#cat-submodule-readme
