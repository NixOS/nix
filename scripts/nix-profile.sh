if test -z "$NIX_SET"; then

    export NIX_SET=1

    NIX_LINKS=/nix/var/nix/links/current

    export PATH=$NIX_LINKS/bin:/nix/bin:$PATH

    export LD_LIBRARY_PATH=$NIX_LINKS/lib:$LD_LIBRARY_PATH

    export LIBRARY_PATH=$NIX_LINKS/lib:$LIBRARY_PATH

    export C_INCLUDE_PATH=$NIX_LINKS/include:$C_INCLUDE_PATH

fi