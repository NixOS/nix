(builtins.imap 0 (i: v: "${v}-${toString i}") [
  "a"
  "b"
])
++ (builtins.imap 1 (i: v: "${v}-${toString i}") [
  "a"
  "b"
])
