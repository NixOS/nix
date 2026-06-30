#!/usr/bin/env bash

source common.sh

authDir="$HOME/.local/share/nix/auth"
mkdir -p "$authDir"

printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n" > "$authDir/example.org"

# A matching user name returns the stored password.
[[ $(printf "protocol=https\nhost=example.org\nusername=alice\n" | nix auth fill) \
   = $(printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n") ]]

# A request without a user name gets both filled in.
[[ $(printf "protocol=https\nhost=example.org\n" | nix auth fill) \
   = $(printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n") ]]

# A request without a protocol is rejected.
expect 1 bash -c 'printf "host=example.org\n" | nix auth fill'

# A non-matching user name returns nothing.
[[ -z $(printf "protocol=https\nhost=example.org\nusername=bob\n" | nix auth fill) ]]

# A password with surrounding whitespace is preserved verbatim.
printf 'protocol=https\nhost=spaces.org\nusername=alice\npassword= sp ace \n' > "$authDir/spaces.org"
[[ $(printf "protocol=https\nhost=spaces.org\n" | nix auth fill) \
   = $(printf 'protocol=https\nhost=spaces.org\nusername=alice\npassword= sp ace \n') ]]

# A file without a host is ignored (must not match every host).
printf "protocol=https\nusername=mallory\npassword=leak\n" > "$authDir/nohost"
[[ -z $(printf "protocol=https\nhost=anywhere.org\n" | nix auth fill) ]]
rm "$authDir/nohost"

# Without an askpass helper, an unsatisfiable required request returns nothing.
unset SSH_ASKPASS
[[ -z $(printf "protocol=https\nhost=fnord.org\nusername=bob\n" | nix auth fill --require) ]]

# Interactive prompting via $SSH_ASKPASS.
askpass="$TEST_ROOT/ask-pass"
cat > "$askpass" <<EOF
#! $SHELL
if [[ \$1 =~ Password ]]; then
   printf "foobar"
elif [[ \$1 =~ Username ]]; then
   printf "alice"
else
   exit 1
fi
EOF
chmod +x "$askpass"
export SSH_ASKPASS="$askpass"

[[ $(printf "protocol=https\nhost=fnord.org\nusername=bob\n" | nix auth fill --require) \
   = $(printf "protocol=https\nhost=fnord.org\nusername=bob\npassword=foobar\n") ]]

[[ $(printf "protocol=https\nhost=fnord.org\n" | nix auth fill --require) \
   = $(printf "protocol=https\nhost=fnord.org\nusername=alice\npassword=foobar\n") ]]

# With store-auth, prompted credentials are saved and reused later.
printf "protocol=https\nhost=stored.org\n" | nix auth fill --require --store-auth > /dev/null
unset SSH_ASKPASS
[[ $(printf "protocol=https\nhost=stored.org\n" | nix auth fill) \
   = $(printf "protocol=https\nhost=stored.org\nusername=alice\npassword=foobar\n") ]]

# Stored credential files must not be world/group readable.
storedFile=$(echo "$authDir"/auto-stored.org-*)
perm=$(stat -c %a "$storedFile" 2>/dev/null || stat -f %Lp "$storedFile")
[[ $perm = 600 ]]

# External credential helpers.
helper="$TEST_ROOT/auth-helper"
cat > "$helper" <<EOF
#! $SHELL
if [[ \$1 == get ]]; then
    host=
    while read line; do
        [[ \$line == host=* ]] && host=\${line:5}
    done
    [[ \$host == bla.org ]] && printf "username=bob\npassword=xyzzy\n"
fi
EOF
chmod +x "$helper"

[[ $(printf "protocol=https\nhost=bla.org\n" | nix auth fill --auth-sources "builtin:nix $helper") \
   = $(printf "protocol=https\nhost=bla.org\nusername=bob\npassword=xyzzy\n") ]]

[[ -z $(printf "protocol=https\nhost=bla2.org\n" | nix auth fill --auth-sources "builtin:nix $helper") ]]
