source common.sh

# Isolate the home for this test.
# Other tests (e.g. flake registry tests) could be writing to $HOME in parallel.
export HOME=$TEST_ROOT/userhome

# Test that using XDG_CONFIG_HOME works
# Assert the config folder didn't exist initially.
[ ! -e "$HOME/.config" ]
# Without XDG_CONFIG_HOME, creates $HOME/.config
unset XDG_CONFIG_HOME
# Run against the nix registry to create the config dir
# (Tip: this relies on removing non-existent entries being a no-op!)
nix registry remove userhome-without-xdg
# Verifies it created it
[ -e "$HOME/.config" ]
# Remove the directory it created
rm -rf "$HOME/.config"
# Run the same test, but with XDG_CONFIG_HOME
export XDG_CONFIG_HOME=$TEST_ROOT/confighome
# Assert the XDG_CONFIG_HOME/nix path does not exist yet.
[ ! -e "$TEST_ROOT/confighome/nix" ]
nix registry remove userhome-with-xdg
# Verifies the confighome path has been created
[ -e "$TEST_ROOT/confighome/nix" ]
# Assert the .config folder hasn't been created.
[ ! -e "$HOME/.config" ]

# Test that files are loaded from XDG by default
export XDG_CONFIG_HOME=$TEST_ROOT/confighome
export XDG_CONFIG_DIRS=$TEST_ROOT/dir1:$TEST_ROOT/dir2
files=$(nix-build --verbose --version | grep "User config" | cut -d ':' -f2- | xargs)
[[ $files == "$TEST_ROOT/confighome/nix/nix.conf:$TEST_ROOT/dir1/nix/nix.conf:$TEST_ROOT/dir2/nix/nix.conf" ]]

# Test that setting NIX_USER_CONF_FILES overrides all the default user config files
export NIX_USER_CONF_FILES=$TEST_ROOT/file1.conf:$TEST_ROOT/file2.conf
files=$(nix-build --verbose --version | grep "User config" | cut -d ':' -f2- | xargs)
[[ $files == "$TEST_ROOT/file1.conf:$TEST_ROOT/file2.conf" ]]

# Test that it's possible to load the config from a custom location
here=$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")
export NIX_USER_CONF_FILES=$here/config/nix-with-substituters.conf
var=$(nix show-config | grep '^substituters =' | cut -d '=' -f 2 | xargs)
[[ $var == https://example.com ]]

# Test that it's possible to load config from the environment
prev=$(nix show-config | grep '^cores' | cut -d '=' -f 2 | xargs)
export NIX_CONFIG="cores = 4242"$'\n'"experimental-features = nix-command flakes"
exp_cores=$(nix show-config | grep '^cores' | cut -d '=' -f 2 | xargs)
exp_features=$(nix show-config | grep '^experimental-features' | cut -d '=' -f 2 | xargs)
[[ $prev != $exp_cores ]]
[[ $exp_cores == "4242" ]]
[[ $exp_features == "flakes nix-command" ]]
