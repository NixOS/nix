To update https://hub.docker.com/r/nixos/nix/

    $ docker build . -t nixos/nix:2.0
    $ docker tag nixos/nix:2.0 nixos/nix:latest
    $ docker push nixos/nix:latest
    $ docker push nixos/nix:2.0

Write access: @domenkozar
