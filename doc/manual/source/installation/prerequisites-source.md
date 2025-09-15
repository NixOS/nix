# Prerequisites

  - GNU Autoconf (<https://www.gnu.org/software/autoconf/>) and the
    autoconf-archive macro collection
    (<https://www.gnu.org/software/autoconf-archive/>). These are
    needed to run the bootstrap script.

  - GNU Make.

  - Bash Shell. The `./configure` script relies on bashisms, so Bash is
    required.

  - A version of GCC or Clang that supports C++23.

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

  - cURL and its library. If your distribution does not provide it, you
    can get it from <https://curl.haxx.se/>.

  - The SQLite embedded database library, version 3.6.19 or higher. If
    your distribution does not provide it, please install it from
    <http://www.sqlite.org/>.

  - The [Boehm garbage collector (`bdw-gc`)](http://www.hboehm.info/gc/) to reduce
    the evaluator’s memory consumption (optional).

    To enable it, install
    `pkgconfig` and the Boehm garbage collector, and pass the flag
    `--enable-gc` to `configure`.

  - The `boost` library of version 1.66.0 or higher. It can be obtained
    from the official web site <https://www.boost.org/>.

  - The `editline` library of version 1.14.0 or higher. It can be
    obtained from the its repository
    <https://github.com/troglobit/editline>.

  - The `libsodium` library for verifying cryptographic signatures
    of contents fetched from binary caches.
    It can be obtained from the official web site
    <https://libsodium.org>.

  - Recent versions of Bison and Flex to build the parser. (This is
    because Nix needs GLR support in Bison and reentrancy support in
    Flex.) For Bison, you need version 2.6, which can be obtained from
    the [GNU FTP server](ftp://alpha.gnu.org/pub/gnu/bison). For Flex,
    you need version 2.5.35, which is available on
    [SourceForge](http://lex.sourceforge.net/). Slightly older versions
    may also work, but ancient versions like the ubiquitous 2.5.4a
    won't.

  - The `libseccomp` is used to provide syscall filtering on Linux. This
    is an optional dependency and can be disabled passing a
    `--disable-seccomp-sandboxing` option to the `configure` script (Not
    recommended unless your system doesn't support `libseccomp`). To get
    the library, visit <https://github.com/seccomp/libseccomp>.

  - On 64-bit x86 machines only, `libcpuid` library
    is used to determine which microarchitecture levels are supported
    (e.g., as whether to have `x86_64-v2-linux` among additional system types).
    The library is available from its homepage
    <http://libcpuid.sourceforge.net>.
    This is an optional dependency and can be disabled
    by providing a `--disable-cpuid` to the `configure` script.

  - Unless `./configure --disable-unit-tests` is specified, GoogleTest (GTest) and
    RapidCheck are required, which are available at
    <https://google.github.io/googletest/> and
    <https://github.com/emil-e/rapidcheck> respectively.
