source common.sh

export REMOTE_STORE="dummy://"

simpleTest () {
    local expr=$1; shift
    local result=$1; shift
    # rest, extra args

    [[ "$(nix eval --impure --raw "$@" --expr "$expr")" == "$result" ]]
}

# `builtins.storeDir`

## Store dir follows `store` store setting
simpleTest 'builtins.storeDir' '/foo' --store "$REMOTE_STORE?store=/foo"
simpleTest 'builtins.storeDir' '/bar' --store "$REMOTE_STORE?store=/bar"

# `builtins.currentSystem`

## `system` alone affects by default
simpleTest 'builtins.currentSystem' 'foo' --system 'foo'
simpleTest 'builtins.currentSystem' 'bar' --system 'bar'

## `system` affects if `eval-system` is an empty string
simpleTest 'builtins.currentSystem' 'foo' --system 'foo' --eval-system ''
simpleTest 'builtins.currentSystem' 'bar' --system 'bar' --eval-system ''

## `eval-system` alone affects
simpleTest 'builtins.currentSystem' 'foo' --eval-system 'foo'
simpleTest 'builtins.currentSystem' 'bar' --eval-system 'bar'

## `eval-system` overrides `system`
simpleTest 'builtins.currentSystem' 'bar' --system 'foo' --eval-system 'bar'
simpleTest 'builtins.currentSystem' 'baz' --system 'foo' --eval-system 'baz'
