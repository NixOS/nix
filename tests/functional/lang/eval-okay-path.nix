[
  (builtins.path
    { path = ./.;
      filter = path: _: baseNameOf path == "data";
      recursive = true;
      sha256 = "1yhm3gwvg5a41yylymgblsclk95fs6jy72w0wv925mmidlhcq4sw";
      name = "output";
    })
  (builtins.path
    { path = ./data;
      recursive = false;
      sha256 = "0k4lwj58f2w5yh92ilrwy9917pycipbrdrr13vbb3yd02j09vfxm";
      name = "output";
    })
]
