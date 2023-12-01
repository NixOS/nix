builtins.path
  { path = ./.;
    filter = path: _: baseNameOf path == "data";
    recursive = true;
    sha256 = "1yhm3gwvg5a41yylymgblsclk95fs6jy72w0wv925mmidlhcq4sw";
    name = "output";
  }
