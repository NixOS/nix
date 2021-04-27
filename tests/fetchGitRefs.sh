source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping Git tests"
    exit 99
fi

clearStore

repo="$TEST_ROOT/git"

rm -rf "$repo" "${repo}-tmp" "$TEST_HOME/.cache/nix"

git init "$repo"
git -C "$repo" config user.email "foobar@example.com"
git -C "$repo" config user.name "Foobar"

echo utrecht > "$repo"/hello
git -C "$repo" add hello
git -C "$repo" commit -m 'Bla1'

path=$(nix eval --raw --impure --expr "(builtins.fetchGit { url = $repo; ref = \"master\"; }).outPath")

# Test various combinations of ref names
# (taken from the git project)

# git help check-ref-format
#       Git imposes the following rules on how references are named:
#
#        1. They can include slash / for hierarchical (directory) grouping, but no slash-separated component can begin with a dot .  or end with the sequence .lock.
#        2. They must contain at least one /. This enforces the presence of a category like heads/, tags/ etc. but the actual names are not restricted. If the --allow-onelevel option is used, this rule is waived.
#        3. They cannot have two consecutive dots ..  anywhere.
#        4. They cannot have ASCII control characters (i.e. bytes whose values are lower than \040, or \177 DEL), space, tilde ~, caret ^, or colon : anywhere.
#        5. They cannot have question-mark ?, asterisk *, or open bracket [ anywhere. See the --refspec-pattern option below for an exception to this rule.
#        6. They cannot begin or end with a slash / or contain multiple consecutive slashes (see the --normalize option below for an exception to this rule)
#        7. They cannot end with a dot ..
#        8. They cannot contain a sequence @{.
#        9. They cannot be the single character @.
#       10. They cannot contain a \.

valid_ref() {
    { set +x; printf >&2 '\n>>>>>>>>>> valid_ref %s\b <<<<<<<<<<\n' $(printf %s "$1" | sed -n -e l); set -x; }
    git check-ref-format --branch "$1" >/dev/null
    git -C "$repo" branch "$1" master >/dev/null
    path1=$(nix eval --raw --impure --expr "(builtins.fetchGit { url = $repo; ref = ''$1''; }).outPath")
    [[ $path1 = $path ]]
    git -C "$repo" branch -D "$1" >/dev/null
}

invalid_ref() {
    { set +x; printf >&2 '\n>>>>>>>>>> invalid_ref %s\b <<<<<<<<<<\n' $(printf %s "$1" | sed -n -e l); set -x; }
    # special case for a sole @:
    # --branch @ will try to interpret @ as a branch reference and not fail. Thus we need --allow-onelevel
    if [ "$1" = "@" ]; then
        (! git check-ref-format --allow-onelevel "$1" >/dev/null 2>&1)
    else
        (! git check-ref-format --branch "$1" >/dev/null 2>&1)
    fi
    nix --debug eval --raw --impure --expr "(builtins.fetchGit { url = $repo; ref = ''$1''; }).outPath" 2>&1 | grep 'invalid Git branch/tag name' >/dev/null
}


valid_ref 'foox'
valid_ref '1337'
valid_ref 'foo.baz'
valid_ref 'foo/bar/baz'
valid_ref 'foo./bar'
valid_ref 'heads/foo@bar'
valid_ref "$(printf 'heads/fu\303\237')"
valid_ref 'foo-bar-baz'
valid_ref '$1'
valid_ref 'foo.locke'

invalid_ref 'refs///heads/foo'
invalid_ref 'heads/foo/'
invalid_ref '///heads/foo'
invalid_ref '.foo'
invalid_ref './foo'
invalid_ref './foo/bar'
invalid_ref 'foo/./bar'
invalid_ref 'foo/bar/.'
invalid_ref 'foo bar'
invalid_ref 'foo?bar'
invalid_ref 'foo^bar'
invalid_ref 'foo~bar'
invalid_ref 'foo:bar'
invalid_ref 'foo[bar'
invalid_ref 'foo/bar/.'
invalid_ref '.refs/foo'
invalid_ref 'refs/heads/foo.'
invalid_ref 'heads/foo..bar'
invalid_ref 'heads/foo?bar'
invalid_ref 'heads/foo.lock'
invalid_ref 'heads///foo.lock'
invalid_ref 'foo.lock/bar'
invalid_ref 'foo.lock///bar'
invalid_ref 'heads/v@{ation'
invalid_ref 'heads/foo\.ar' # should fail due to \
invalid_ref 'heads/foo\bar' # should fail due to \
invalid_ref "$(printf 'heads/foo\t')" # should fail because it has a TAB
invalid_ref "$(printf 'heads/foo\177')"
invalid_ref '@'

invalid_ref 'foo/*'
invalid_ref '*/foo'
invalid_ref 'foo/*/bar'
invalid_ref '*'
invalid_ref 'foo/*/*'
invalid_ref '*/foo/*'
invalid_ref '/foo'
invalid_ref ''
