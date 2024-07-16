source common.sh

authDir="$HOME/.local/share/nix/auth"
mkdir -p "$authDir"

printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n" > $authDir/example.org

[[ $(printf "protocol=https\nhost=example.org\nusername=alice\n" | nix auth fill) = $(printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n") ]]
[[ $(printf "protocol=https\nhost=example.org\n" | nix auth fill) = $(printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n") ]]
(! printf "host=example.org\n" | nix auth fill)
[[ $(printf "protocol=https\nhost=example.org\nusername=bob\n" | nix auth fill) = "" ]]

# Test interactive prompting.
unset SSH_ASKPASS
[[ $(printf "protocol=https\nhost=fnord.org\nusername=bob\n" | nix auth fill --require) = "" ]]

askpass="$TEST_ROOT/ask-pass"
cat > $askpass <<EOF
#! $SHELL
prompt="\$1" >&2
if [[ \$prompt =~ Password ]]; then
   printf "foobar"
elif [[ \$prompt =~ Username ]]; then
   printf "alice"
else
   exit 1
fi
EOF
chmod +x $askpass
export SSH_ASKPASS=$askpass

[[ $(printf "protocol=https\nhost=fnord.org\nusername=bob\n" | nix auth fill --require) = $(printf "protocol=https\nhost=fnord.org\nusername=bob\npassword=foobar\n") ]]

[[ $(printf "protocol=https\nhost=fnord.org\n" | nix auth fill --require) = $(printf "protocol=https\nhost=fnord.org\nusername=alice\npassword=foobar\n") ]]

# Test storing authentication.
[[ $(printf "protocol=https\nhost=fnord.org\n" | nix auth fill --require --store-auth) = $(printf "protocol=https\nhost=fnord.org\nusername=alice\npassword=foobar\n") ]]
unset SSH_ASKPASS
[[ $(printf "protocol=https\nhost=fnord.org\n" | nix auth fill --require --store-auth) = $(printf "protocol=https\nhost=fnord.org\nusername=alice\npassword=foobar\n") ]]

# Test authentication helpers.
myHelper="$TEST_ROOT/auth-helper"
cat > $myHelper <<EOF
#! $SHELL

if [[ \$1 == get ]]; then
    host=
    while read line; do
        if [[ \$line == host=* ]]; then
            host=\${line:5}
        fi
    done
    if [[ \$host == bla.org ]]; then
        printf "username=bob\npassword=xyzzy\n"
    fi
fi
EOF
chmod +x $myHelper

[[ $(printf "protocol=https\nhost=bla.org\n" | nix auth fill --extra-experimental-features 'pluggable-auth' --auth-sources "builtin:nix $myHelper") = $(printf "protocol=https\nhost=bla.org\nusername=bob\npassword=xyzzy\n") ]]
[[ -z $(printf "protocol=https\nhost=bla2.org\n" | nix auth fill --extra-experimental-features 'pluggable-auth' --auth-sources "builtin:nix $myHelper") ]]