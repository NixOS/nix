# Make sure the expected XML output is produced; in particular, make sure it
# doesn't contain source location information.
builtins.toXML { a = "s"; }
