source common.sh

clearStore

# test help output

nix-build --help
nix-shell --help

nix-env --help
nix-env --install --help
nix-env --upgrade --help
nix-env --uninstall --help
nix-env --set --help
nix-env --set-flag --help
nix-env --query --help
nix-env --switch-profile --help
nix-env --list-generations --help
nix-env --delete-generations --help
nix-env --switch-generation --help
nix-env --rollback --help

nix-store --help
nix-store --realise --help
nix-store --serve --help
nix-store --gc --help
nix-store --delete --help
nix-store --query --help
nix-store --add --help
nix-store --add-fixed --help
nix-store --verify --help
nix-store --verify-path --help
nix-store --repair-path --help
nix-store --dump --help
nix-store --restore --help
nix-store --export --help
nix-store --import --help
nix-store --optimise --help
nix-store --read-log --help
nix-store --dump-db --help
nix-store --load-db --help
nix-store --print-env --help
nix-store --generate-binary-cache-key --help

nix-channel --help
nix-collect-garbage --help
nix-copy-closure --help
nix-daemon --help
nix-hash --help
nix-instantiate --help
nix-prefetch-url --help

function subcommands() {
  jq -r '
def recurse($prefix):
    to_entries[] |
    ($prefix + [.key]) as $newPrefix |
    (if .value | has("commands") then
      ($newPrefix, (.value.commands | recurse($newPrefix)))
    else
      $newPrefix
    end);
.args.commands | recurse([]) | join(" ")
'
}

nix __dump-cli | subcommands | while IFS= read -r cmd; do
    nix $cmd --help
done
