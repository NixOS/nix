[
  # NAR hash of directory with filter
  (builtins.path {
    path = ./..;
    filter = path: _: baseNameOf path == "data";
    recursive = true;
    sha256 = "1yhm3gwvg5a41yylymgblsclk95fs6jy72w0wv925mmidlhcq4sw";
    name = "output";
  })
  # Flat hash of file
  (builtins.path {
    path = ../data;
    recursive = false;
    sha256 = "0k4lwj58f2w5yh92ilrwy9917pycipbrdrr13vbb3yd02j09vfxm";
    name = "output";
  })
  # NAR hash of directory (recursive = true is the default)
  (builtins.path {
    path = ../dir1;
    sha256 = "02vlkcjkl1rvy081n6d40qi73biv2w4b9x9biklay4ncgk77zr1f";
    name = "output";
  })
]
