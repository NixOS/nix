source common.sh

if [[ $(uname -ms) = "Linux x86_64" ]]; then
    # x86_64 CPUs must always support the baseline
    # microarchitecture level.
    nix -vv --version | grep -q "x86_64-v1-linux"
fi
