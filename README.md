Nix, the purely functional package manager
------------------------------------------

Nix is a new take on package management that is fairly unique. Because of it's
purity aspects, a lot of issues found in traditional package managers don't
appear with Nix.

To find out more about the tool, usage and installation instructions, please
read the manual, which is available on the Nix website at
<http://nixos.org/nix/manual>.


## Prerequisites

   These are prerequisites for NiX installation.

  1. GNU Make

  2. A version of GCC or Clang that supports C++11.

  3. Perl 5.8 or higher

  4. pkg-config to locate dependencies. If your distribution does
     not provide it, you can get it from
     http://www.freedesktop.org/wiki/Software/pkg-config

  5. The bzip2 compressor program and the `libbz2' library.
     Thus you must have bzip2 installed, including development
     headers and libraries.  If your distribution does not provide
     these, you can obtain bzip2 from http://www.bzip.org

  6. The SQLite embedded database library, version 3.6.19 or higher.
     If your distribution does not provide it, please install it from
     http://www.sqlite.org/ .

  7. The Perl DBI, DBD::SQLite, and WWW::Curl libraries, which are
     available from http://search.cpan.org/ if your distribution does
     not provide them.

  8. The Boehm garbage collector (http://www.hboehm.info/gc/)
     to reduce the evaluator’s memory consumption (optional).  To
     enable it, install `pkgconfig' and the Boehm garbage collector,
     and pass the flag `--enable-gc' to 'configure'.

  9. The `xmllint' and `xsltproc' programs to build this manual
     and the man-pages.  These are part of the `libxml2' and `libxslt'
     packages, respectively.  You also need the DocBook
     (http://docbook.sourceforge.net/projects/xsl/) XSL stylesheets
     and optionally the DocBook 5.0 RELAX NG schemas
     (http://www.docbook.org/schemas/5x).  Note that these are only
     required if you modify the manual sources or when you are building
     from the Git repository.

  10. Recent versions of Bison and Flex to build the parser.
     (This is because Nix needs GLR support in Bison and reentrancy
     support in Flex.)  For Bison, you need version 2.6, which can be
     obtained from the GNU FTP Server
     (ftp://alpha.gnu.org/pub/gnu/bison).
     For Flex, you need version 2.5.35, which is available on SourceForge
     (http://lex.sourceforge.net/).  Slightly older versions may also
     work, but ancient versions like the ubiquitous 2.5.4a won't.  Note
     that these are only required if you modify the parser or when you
     are building from the Git repository.


## License

Nix is released under the LGPL v2.1

This product includes software developed by the OpenSSL Project for
use in the OpenSSL Toolkit (http://www.OpenSSL.org/).
