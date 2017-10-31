source common.sh

strip_colors() {
    sed -r "s/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g"
}

check() {
    exp="$1"

    (
        tmp=$(mktemp)
        cleanup() {
            rm "$tmp"
        }
        trap cleanup EXIT

        ( echo "$exp"
          echo ":q"
        ) | nix repl \
          | grep '|' \
          | strip_colors \
                >"$tmp"

        diff -U10 - "$tmp"
    )
}

check "(import ./comments.nix).f" <<EOF
| f
| -
| 
| Just a function.
EOF

check "(import ./comments.nix).nested.f" <<EOF
| f
| -
| 
| Just a function in an attrset.
EOF

check "(import ./comments.nix).g" <<EOF
| g
| -
| 
| A nice function.
EOF

check "(import ./comments.nix).h" <<EOF
| h
| -
| 
| A somewhat nice function.
EOF

check "(import ./comments.nix).i" <<EOF
| i
| -
| 
| A nice function.
EOF

check "(import ./comments.nix).j" <<EOF
| j
| -
| 
| A nice function.
EOF

check "(import ./comments.nix).k" <<EOF
| k
| -
| 
| A nice function.
EOF

check "(import ./comments.nix).l" <<EOF
| l
| -
| 
| A nice function.
EOF

check "(import ./comments.nix).m" <<EOF
| m
| -
| 
| A nice function.
EOF

check "(import ./comments.nix).n" <<EOF
| n
| -
| 
| One
| Two
EOF

check "(import ./comments.nix).o" <<EOF
| o
| -
| 
| Bullets:
| 
|  * Are lethal.
|  * Are made of metal.
EOF

check "(import ./comments.nix).p" <<EOF
| p
| -
| 
| Bullets:
| 
|  * Are lethal.
|  * Are made of metal.
EOF

check "(import ./comments.nix).q" <<EOF
| q
| -
| 
| Useful stuff
EOF


check "(import ./comments.nix).r" <<EOF
| r
| -
| 
| Useful
| stuff
EOF

check "(import ./comments.nix).unicode1" <<EOF
| unicode1
| --------
| 
| ßuper toll.
EOF

check "(import ./comments.nix).unicode2" <<EOF
| unicode2
| --------
| 
| 🤢
EOF

check "(import ./comments.nix).curried" <<EOF
| curried
| -------
| 
| Apply me twice.
EOF

check "(import ./comments.nix).curried true" <<EOF
| curried
| -------
| 
| NOTE: This function has already been applied!
|       You should ignore the first 1 parameter(s) in this documentation,
|       because they have already been applied.
|
| Apply me twice.
EOF

check "(import ./comments.nix).curried2 true false" <<EOF
| curried2
| --------
| 
| NOTE: This function has already been applied!
|       You should ignore the first 2 parameter(s) in this documentation,
|       because they have already been applied.
|
| You can give 3 arguments.
EOF

check "(import ./comments.nix).curried2 true" <<EOF
| curried2
| --------
| 
| NOTE: This function has already been applied!
|       You should ignore the first 1 parameter(s) in this documentation,
|       because they have already been applied.
|
| You can give 3 arguments.
EOF
