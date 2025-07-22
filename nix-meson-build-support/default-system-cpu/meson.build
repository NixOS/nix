powerpc_system_cpus = [ 'ppc64', 'ppc' ]

nix_system_cpu = {'ppc64' : 'powerpc64', 'ppc' : 'powerpc'}.get(
  host_machine.cpu_family(),
  host_machine.cpu_family(),
)

if powerpc_system_cpus.contains(host_machine.cpu_family()) and host_machine.endian() == 'little'
  nix_system_cpu += 'le'
endif
