source common.sh

authDir="$HOME/.local/share/nix/auth"
mkdir -p "$authDir"

printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n" > $authDir/example.org

[[ $(printf "protocol=https\nhost=example.org\nusername=alice\n" | nix auth fill) = $(printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n") ]]
[[ $(printf "protocol=https\nhost=example.org\n" | nix auth fill) = $(printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n") ]]
(! printf "host=example.org\n" | nix auth fill)
[[ $(printf "protocol=https\nhost=example.org\nusername=bob\n" | nix auth fill) = "" ]]

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

[[ $(printf "protocol=https\nhost=bla.org\n" | nix auth fill --auth-sources "builtin:nix $myHelper") = $(printf "protocol=https\nhost=bla.org\nusername=bob\npassword=xyzzy\n") ]]
[[ -z $(printf "protocol=https\nhost=bla2.org\n" | nix auth fill --auth-sources "builtin:nix $myHelper") ]]
