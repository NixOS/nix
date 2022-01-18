source common.sh

clearStore

cp ./dependencies.nix ./dependencies.builder0.sh ./config.nix $TEST_HOME

cd $TEST_HOME

nix-build ./dependencies.nix -A input0_drv -o dep
nix-build ./dependencies.nix -o toplevel

nix why-depends ./toplevel ./dep |
    grep input-2
