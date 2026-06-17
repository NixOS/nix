# Prerequisites

  This list and lower version bounds are maintained on best-effort basis. When in doubt, check the `meson.build` files.

  - Meson build system (<https://github.com/mesonbuild/meson>).

  - Ninja (<https://ninja-build.org/>).

  - A version of GCC or Clang that supports C++23 (anything newer than Clang 19 or GCC 14 is likely to work).

  - `pkg-config` to locate dependencies.
    If your distribution does not provide it, you can get it from <http://www.freedesktop.org/wiki/Software/pkg-config>.

  - The OpenSSL library to calculate cryptographic hashes.
    If your distribution does not provide it, you can get it from <https://www.openssl.org>.

  - The `libbrotlienc` and `libbrotlidec` libraries to provide implementation of the Brotli compression algorithm.
    They are available for download from the official repository <https://github.com/google/brotli>.

  - cURL library.
    If your distribution does not provide it, you can get it from <https://curl.haxx.se/>.

  - The SQLite embedded database library, version 3.6.19 or higher.
    If your distribution does not provide it, please install it from <http://www.sqlite.org/>.

  - The [Boehm garbage collector (`bdw-gc`)](http://www.hboehm.info/gc/) to reduce the evaluator’s memory consumption (optional).
    To enable it, install `pkgconfig` and the Boehm garbage collector, and pass the option `-Dlibexpr:gc=enabled` to `meson setup`.

  - The `boost` library of version 1.87.0 or higher.
    It can be obtained from the official web site <https://www.boost.org/>.

  - The `editline` library of version 1.14.0 or higher.
    It can be obtained from the its repository <https://github.com/troglobit/editline>.

  - The `libsodium` library for verifying cryptographic signatures of contents fetched from binary caches.
    It can be obtained from the official web site <https://libsodium.org>.

  - Recent versions of Bison and Flex to build the parser.
    (This is because Nix needs C++ template support in Bison and reentrancy support in Flex.)

  - The `libseccomp` is used to provide syscall filtering on Linux.
    This is an optional dependency and can be disabled passing a `-Dlibstore:seccomp-sandboxing=disabled` option to the `meson setup` command
    (Not recommended unless your system doesn't support `libseccomp`).
    To get the library, visit <https://github.com/seccomp/libseccomp>.

  - On 64-bit x86 machines only, `libcpuid` library is used to determine which microarchitecture levels are supported
    (e.g., as whether to have `x86_64-v2-linux` among additional system types).
    The library is available from its homepage <http://libcpuid.sourceforge.net>.
    This is an optional dependency and can be disabled by providing a `-Dlibutil:cpuid=disabled` option to `meson setup` script.

  - Unless `meson setup build -Dunit-tests=false` is specified, GoogleTest (GTest) and RapidCheck are required, which are available at
    <https://google.github.io/googletest/> and <https://github.com/emil-e/rapidcheck> respectively.
