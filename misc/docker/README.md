To update https://hub.docker.com/r/nixos/nix/

    $ docker build .
    $ docker image ls
    $ docker tag <hash> nixos/nix:2.0
    $ docker tag <hash> nixos/nix:latest
    $ docker push nixos/nix:latest
    $ docker push nixos/nix:2.0

Write access: @domenkozar
