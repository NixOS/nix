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
    echo FNORD > $repo/README.md
    git -C $repo add README.md
    git -C $repo commit -m 'Initial'
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

        mapAttrsToList = f: attrs:
          map (name: f name attrs.\${name}) (builtins.attrNames attrs);
        pipe = val: functions:
          let reverseApply = x: f: f x;
          in builtins.foldl' reverseApply val functions;
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

        combineModules = self: let
          srcs = builtins.mapAttrs (_: url: fetchTree (traceVal url)) self.modules;
        in mkDerivation {
          name = "source-with-submodules";
          buildCommand = ''
            cp -r \${self} \$out
            chmod -R +w \$_

            \${
              pipe srcs [
                (mapAttrsToList (path: src: "cp -rT \${src} \$out/\${path}"))
                (builtins.concatStringsSep "\n")
              ]
            }
          '';
        };
    in {
        packages.$system = {
            cat-submodule-readme = writeShellScriptBin "cat-submodule-readme" ''
                cat \${nonFlake}/README.md
            '';
            use-submodule-as-flake = flake.packages.$system.cat-own-readme;

            cat-own-readme = writeShellScriptBin "cat-own-readme" ''
                cat \${self}/README.md
            '';

            source-with-submodules = combineModules self;
        };

        defaultPackage.$system = self.packages.$system.cat-submodule-readme;
    };
}
EOF

git -C $flakeWithSubmodules add .
git -C $flakeWithSubmodules commit -m'add flake.nix'

[[ $(nix run $flakeWithSubmodules#cat-own-readme) == "FNORD" ]]
[[ $(nix run $flakeWithSubmodules#cat-submodule-readme) == "FNORD" ]]
[[ $(nix run $flakeWithSubmodules#use-submodule-as-flake) == "FNORD" ]]
nix build -o $TEST_ROOT/result $flakeWithSubmodules#source-with-submodules
[[ $(cat $TEST_ROOT/result/nonFlake/README.md) == "FNORD" ]]

echo FOST > $flakeWithSubmodules/README.md

# apply dirt
echo FSUD > $flakeWithSubmodules/nonFlake/README.md
[[ $(nix run $flakeWithSubmodules#cat-submodule-readme) == "FSUD" ]]
nix build -o $TEST_ROOT/result $flakeWithSubmodules#source-with-submodules
[[ $(cat $TEST_ROOT/result/nonFlake/README.md) == "FSUD" ]]

# should work for flake as well
echo FSUD > $flakeWithSubmodules/flake/README.md
[[ $(nix run $flakeWithSubmodules#use-submodule-as-flake) == "FSUD" ]]
