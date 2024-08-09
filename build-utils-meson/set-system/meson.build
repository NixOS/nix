host_cpu = host_machine.cpu_family()
if host_cpu in ['x86', 'i686', 'i386']
  # Meson considers 32-bit x86 CPUs to be "x86", and does not consider 64-bit
  # x86 CPUs to be "x86" (instead using "x86_64", which needs no translation).
  host_cpu = 'i686'
elif host_cpu == 'amd64'
  # This should not be needed under normal circumstances, but someone could pass a --cross-file
  # that sets the cpu_family to this.
  host_cpu = 'x86_64'
elif host_cpu in ['armv6', 'armv7']
  host_cpu += 'l'
endif

host_system = host_machine.cpu_family() + '-' + host_machine.system()
message('canonical Nix system name:', host_system)

is_linux = host_machine.system() == 'linux'
is_darwin = host_machine.system() == 'darwin'
is_freebsd = host_machine.system() == 'freebsd'
is_x64 = host_machine.cpu_family() == 'x86_64'
