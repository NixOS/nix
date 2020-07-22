# Prerequisites

  - GNU Autoconf (<https://www.gnu.org/software/autoconf/>) and the
    autoconf-archive macro collection
    (<https://www.gnu.org/software/autoconf-archive/>). These are only
    needed to run the bootstrap script, and are not necessary if your
    source distribution came with a pre-built `./configure` script.

  - GNU Make.

  - Bash Shell. The `./configure` script relies on bashisms, so Bash is
    required.

  - A version of GCC or Clang that supports C++17.

  - `pkg-config` to locate dependencies. If your distribution does not
    provide it, you can get it from
    <http://www.freedesktop.org/wiki/Software/pkg-config>.

  - The OpenSSL library to calculate cryptographic hashes. If your
    distribution does not provide it, you can get it from
    <https://www.openssl.org>.

  - The `libbrotlienc` and `libbrotlidec` libraries to provide
    implementation of the Brotli compression algorithm. They are
    available for download from the official repository
    <https://github.com/google/brotli>.

  - The bzip2 compressor program and the `libbz2` library. Thus you must
    have bzip2 installed, including development headers and libraries.
    If your distribution does not provide these, you can obtain bzip2
    from
    <https://web.archive.org/web/20180624184756/http://www.bzip.org/>.

  - `liblzma`, which is provided by XZ Utils. If your distribution does
    not provide this, you can get it from <https://tukaani.org/xz/>.

  - cURL and its library. If your distribution does not provide it, you
    can get it from <https://curl.haxx.se/>.

  - The SQLite embedded database library, version 3.6.19 or higher. If
    your distribution does not provide it, please install it from
    <http://www.sqlite.org/>.

  - The [Boehm garbage collector](http://www.hboehm.info/gc/) to reduce
    the evaluator’s memory consumption (optional). To enable it, install
    `pkgconfig` and the Boehm garbage collector, and pass the flag
    `--enable-gc` to `configure`.

  - The `boost` library of version 1.66.0 or higher. It can be obtained
    from the official web site <https://www.boost.org/>.

  - The `editline` library of version 1.14.0 or higher. It can be
    obtained from the its repository
    <https://github.com/troglobit/editline>.

  - Recent versions of Bison and Flex to build the parser. (This is
    because Nix needs GLR support in Bison and reentrancy support in
    Flex.) For Bison, you need version 2.6, which can be obtained from
    the [GNU FTP server](ftp://alpha.gnu.org/pub/gnu/bison). For Flex,
    you need version 2.5.35, which is available on
    [SourceForge](http://lex.sourceforge.net/). Slightly older versions
    may also work, but ancient versions like the ubiquitous 2.5.4a
    won't. Note that these are only required if you modify the parser or
    when you are building from the Git repository.

  - The `libseccomp` is used to provide syscall filtering on Linux. This
    is an optional dependency and can be disabled passing a
    `--disable-seccomp-sandboxing` option to the `configure` script (Not
    recommended unless your system doesn't support `libseccomp`). To get
    the library, visit <https://github.com/seccomp/libseccomp>.
