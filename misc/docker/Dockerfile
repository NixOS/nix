FROM alpine

RUN wget -O- http://nixos.org/releases/nix/nix-1.11.2/nix-1.11.2-x86_64-linux.tar.bz2 | bzcat - | tar xf - \
    && echo "nixbld:x:30000:nixbld1,nixbld2,nixbld3,nixbld4,nixbld5,nixbld6,nixbld7,nixbld8,nixbld9,nixbld10,nixbld11,nixbld12,nixbld13,nixbld14,nixbld15,nixbld16,nixbld17,nixbld18,nixbld19,nixbld20,nixbld21,nixbld22,nixbld23,nixbld24,nixbld25,nixbld26,nixbld27,nixbld28,nixbld29,nixbld30" >> /etc/group \
    && for i in $(seq 1 30); do echo "nixbld$i:x:$((30000 + $i)):30000:::" >> /etc/passwd; done \
    && mkdir -m 0755 /nix && USER=root sh nix-*-x86_64-linux/install \
    && echo ". /root/.nix-profile/etc/profile.d/nix.sh" >> /etc/profile \
    && rm -r /nix-*-x86_64-linux

ONBUILD ENV \
    ENV=/etc/profile \
    PATH=/root/.nix-profile/bin:/root/.nix-profile/sbin:/bin:/sbin:/usr/bin:/usr/sbin \
    GIT_SSL_CAINFO=/root/.nix-profile/etc/ssl/certs/ca-bundle.crt \
    SSL_CERT_FILE=/root/.nix-profile/etc/ssl/certs/ca-bundle.crt

ENV \
    ENV=/etc/profile \
    PATH=/root/.nix-profile/bin:/root/.nix-profile/sbin:/bin:/sbin:/usr/bin:/usr/sbin \
    GIT_SSL_CAINFO=/root/.nix-profile/etc/ssl/certs/ca-bundle.crt \
    SSL_CERT_FILE=/root/.nix-profile/etc/ssl/certs/ca-bundle.crt \
    NIX_PATH=/nix/var/nix/profiles/per-user/root/channels/
