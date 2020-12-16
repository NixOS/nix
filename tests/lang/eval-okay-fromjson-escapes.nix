# This string contains all supported escapes in a JSON string, per json.org
# \b and \f are not supported by Nix
builtins.fromJSON ''"quote \" reverse solidus \\ solidus \/ backspace \b formfeed \f newline \n carriage return \r horizontal tab \t 1 char unicode encoded backspace \u0008 1 char unicode encoded e with accent \u00e9 2 char unicode encoded s with caron \u0161 3 char unicode encoded rightwards arrow \u2192"''
