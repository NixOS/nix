source common.sh

authDir="$HOME/.local/share/nix/auth"
mkdir -p "$authDir"

printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n" > $authDir/example.org

[[ $(printf "protocol=https\nhost=example.org\nusername=alice\n" | nix auth fill) = $(printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n") ]]
[[ $(printf "protocol=https\nhost=example.org\n" | nix auth fill) = $(printf "protocol=https\nhost=example.org\nusername=alice\npassword=foobar\n") ]]
(! printf "host=example.org\n" | nix auth fill)
[[ $(printf "protocol=https\nhost=example.org\nusername=bob\n" | nix auth fill) = "" ]]
