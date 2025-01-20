let
  string = "str";
in [
  /some/path

  ''${/some/path}''

  ''
    ${/some/path}''

  ''${/some/path}
    end''

  string

  ''${string}''

  ''
    ${string}''

  ''${string}
    end''

  ''''

  ''
  ''

  ''
    end''
]
