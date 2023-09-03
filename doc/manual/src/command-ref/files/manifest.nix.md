## `manifest.nix`

The manifest file records the provenance of the packages that are installed in a [profile](./profiles.md) managed by [`nix-env`](@docroot@/command-ref/nix-env.md).

Here is an example of how this file might look like after installing `hello` from Nixpkgs:

```nix
[{
  meta = {
    available = true;
    broken = false;
    changelog =
      "https://git.savannah.gnu.org/cgit/hello.git/plain/NEWS?h=v2.12.1";
    description = "A program that produces a familiar, friendly greeting";
    homepage = "https://www.gnu.org/software/hello/manual/";
    insecure = false;
    license = {
      deprecated = false;
      free = true;
      fullName = "GNU General Public License v3.0 or later";
      redistributable = true;
      shortName = "gpl3Plus";
      spdxId = "GPL-3.0-or-later";
      url = "https://spdx.org/licenses/GPL-3.0-or-later.html";
    };
    longDescription = ''
      GNU Hello is a program that prints "Hello, world!" when you run it.
      It is fully customizable.
    '';
    maintainers = [{
      email = "edolstra+nixpkgs@gmail.com";
      github = "edolstra";
      githubId = 1148549;
      name = "Eelco Dolstra";
    }];
    name = "hello-2.12.1";
    outputsToInstall = [ "out" ];
    platforms = [
      "i686-cygwin"
      "x86_64-cygwin"
      "x86_64-darwin"
      "i686-darwin"
      "aarch64-darwin"
      "armv7a-darwin"
      "i686-freebsd13"
      "x86_64-freebsd13"
      "aarch64-genode"
      "i686-genode"
      "x86_64-genode"
      "x86_64-solaris"
      "js-ghcjs"
      "aarch64-linux"
      "armv5tel-linux"
      "armv6l-linux"
      "armv7a-linux"
      "armv7l-linux"
      "i686-linux"
      "m68k-linux"
      "microblaze-linux"
      "microblazeel-linux"
      "mipsel-linux"
      "mips64el-linux"
      "powerpc64-linux"
      "powerpc64le-linux"
      "riscv32-linux"
      "riscv64-linux"
      "s390-linux"
      "s390x-linux"
      "x86_64-linux"
      "mmix-mmixware"
      "aarch64-netbsd"
      "armv6l-netbsd"
      "armv7a-netbsd"
      "armv7l-netbsd"
      "i686-netbsd"
      "m68k-netbsd"
      "mipsel-netbsd"
      "powerpc-netbsd"
      "riscv32-netbsd"
      "riscv64-netbsd"
      "x86_64-netbsd"
      "aarch64_be-none"
      "aarch64-none"
      "arm-none"
      "armv6l-none"
      "avr-none"
      "i686-none"
      "microblaze-none"
      "microblazeel-none"
      "msp430-none"
      "or1k-none"
      "m68k-none"
      "powerpc-none"
      "powerpcle-none"
      "riscv32-none"
      "riscv64-none"
      "rx-none"
      "s390-none"
      "s390x-none"
      "vc4-none"
      "x86_64-none"
      "i686-openbsd"
      "x86_64-openbsd"
      "x86_64-redox"
      "wasm64-wasi"
      "wasm32-wasi"
      "x86_64-windows"
      "i686-windows"
    ];
    position =
      "/nix/store/7niq32w715567hbph0q13m5lqna64c1s-nixos-unstable.tar.gz/nixos-unstable.tar.gz/pkgs/applications/misc/hello/default.nix:34";
    unfree = false;
    unsupported = false;
  };
  name = "hello-2.12.1";
  out = {
    outPath = "/nix/store/260q5867crm1xjs4khgqpl6vr9kywql1-hello-2.12.1";
  };
  outPath = "/nix/store/260q5867crm1xjs4khgqpl6vr9kywql1-hello-2.12.1";
  outputs = [ "out" ];
  system = "x86_64-linux";
  type = "derivation";
}]
```

Each element in this list corresponds to an installed package.
It incorporates some attributes of the original derivation, including `meta`, `name`, `out`, `outPath`, `outputs`, `system`.
This information is used by Nix for querying and updating the package.
