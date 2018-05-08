FROM alpine

# Enable HTTPS support in wget.
RUN apk add --update openssl

# Download Nix and install it into the system.
RUN wget https://nixos.org/releases/nix/nix-2.0.1/nix-2.0.1-x86_64-linux.tar.bz2 \
  && echo "8b8f0b8d3912273b037dcc51497df7bbc9529dbad48aeadf322ca27c4c4c7a90  nix-2.0.1-x86_64-linux.tar.bz2" | sha256sum -c \
  && tar xjf nix-*-x86_64-linux.tar.bz2 \
  && addgroup -g 30000 -S nixbld \
  && for i in $(seq 1 30); do adduser -S -D -h /var/empty -g "Nix build user $i" -u $((30000 + i)) -G nixbld nixbld$i ; done \
  && mkdir -m 0755 /nix && USER=root sh nix-*-x86_64-linux/install \
  && ln -s /nix/var/nix/profiles/default/etc/profile.d/nix.sh /etc/profile.d/ \
  && rm -r /nix-*-x86_64-linux \
  && rm -r /var/cache/apk/*

ONBUILD ENV \
    ENV=/etc/profile \
    PATH=/nix/var/nix/profiles/default/bin:/nix/var/nix/profiles/default/sbin:/bin:/sbin:/usr/bin:/usr/sbin \
    GIT_SSL_CAINFO=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt \
    NIX_SSL_CERT_FILE=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt

ENV \
    ENV=/etc/profile \
    PATH=/nix/var/nix/profiles/default/bin:/nix/var/nix/profiles/default/sbin:/bin:/sbin:/usr/bin:/usr/sbin \
    GIT_SSL_CAINFO=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt \
    NIX_SSL_CERT_FILE=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt \
    NIX_PATH=/nix/var/nix/profiles/per-user/root/channels
