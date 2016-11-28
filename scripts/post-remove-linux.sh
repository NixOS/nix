#!/bin/sh -e

nixbld_user=nixbld
nixbld_group=nixbld


# Remove build users
for i in $(seq 32); do
  if getent passwd "$nixbld_user$i" >/dev/null; then
    userdel "$nixbld_user$i"
  fi
done

if getent group "$nixbld_group" >/dev/null && [ -z "$(getent group "$nixbld_group" | cut -d ':' -f 4)" ]; then
  groupdel "$nixbld_group"
fi
