#!/bin/sh -e

nixbld_user=nixbld
nixbld_group=nixbld
storedir="$(nix-store --verbose --version 2>/dev/null | sed -n 's/^Store directory: //p')"
localstatedir="$(nix-store --verbose --version 2>/dev/null | sed -n 's/^State directory: //p')"


# Setup build users
if ! getent group "$nixbld_group" >/dev/null; then
  groupadd -r "$nixbld_group"
fi

for i in $(seq 32); do
  if ! getent passwd "$nixbld_user$i" >/dev/null; then
    useradd -r -g "$nixbld_group" -G "$nixbld_group" -d /var/empty \
      -s /sbin/nologin \
      -c "Nix build user $i" "$nixbld_user$i"
  fi
done


# Create the store
mkdir -p -m 1777 "$localstatedir/profiles/per-user"
mkdir -p -m 1777 "$localstatedir/gcroots/per-user"
mkdir -p "$localstatedir/channel-cache"
if [ ! -d "$storedir" ]; then
  mkdir -p -m 1775 "$storedir"
  chgrp "$nixbld_group" "$storedir"
  nix-store --init
fi
