# This attempts to translate meson cpu_family and cpu_name specified via
# --cross-file [1] into a nix *system double*. Nixpkgs mostly respects ([2]) the
# conventions outlined in [1].
#
# [1]: https://mesonbuild.com/Reference-tables.html#cpu-families
# [2]: https://github.com/NixOS/nixpkgs/blob/master/pkgs/build-support/lib/meson.nix

nix_system_cpu = {'ppc64' : 'powerpc64', 'ppc' : 'powerpc', 'x86' : 'i686'}.get(
  host_machine.cpu_family(),
  host_machine.cpu_family(),
)

if (host_machine.cpu_family() in [ 'ppc64', 'ppc' ]) and host_machine.endian() == 'little'
  nix_system_cpu += 'le'
elif host_machine.cpu_family() in [ 'mips64', 'mips' ] and host_machine.endian() == 'little'
  nix_system_cpu += 'el'
elif host_machine.cpu_family() == 'arm'
  nix_system_cpu = host_machine.cpu()
endif
