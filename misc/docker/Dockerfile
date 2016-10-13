FROM busybox

RUN set -x \
    && wget -O- http://nixos.org/releases/nix/nix-1.9/nix-1.9-x86_64-linux.tar.bz2 | \
        bzcat - | tar xf - \
    && echo "nixbld:x:30000:nixbld1,nixbld10,nixbld2,nixbld3,nixbld4,nixbld5,nixbld6,nixbld7,nixbld8,nixbld9" >> /etc/group \
    && for i in $(seq 1 9); do echo "nixbld$i:x:3000$i:30000:::" >> /etc/passwd; done \
    && sed -i 's/\$HOME\/\.nix-profile\/etc\/ssl\/certs\/ca-bundle\.crt/\$HOME\/\.nix-profile\/etc\/ca-bundle\.crt/g' nix-1.9-x86_64-linux/install \
    && mkdir -m 0755 /nix && USER=root sh nix-1.9-x86_64-linux/install \
    && echo ". /root/.nix-profile/etc/profile.d/nix.sh" >> /etc/profile \
    && rm -r /nix-1.9-x86_64-linux

ONBUILD ENV \
    ENV=/etc/profile \
    PATH=/root/.nix-profile/bin:/root/.nix-profile/sbin:/bin:/sbin:/usr/bin:/usr/sbin \
    GIT_SSL_CAINFO=/root/.nix-profile/etc/ssl/certs/ca-bundle.crt \
    NIX_SSL_CERT_FILE=/root/.nix-profile/etc/ssl/certs/ca-bundle.crt

ENV \
    ENV=/etc/profile \
    PATH=/root/.nix-profile/bin:/root/.nix-profile/sbin:/bin:/sbin:/usr/bin:/usr/sbin \
    GIT_SSL_CAINFO=/root/.nix-profile/etc/ssl/certs/ca-bundle.crt \
    NIX_SSL_CERT_FILE=/root/.nix-profile/etc/ssl/certs/ca-bundle.crt
