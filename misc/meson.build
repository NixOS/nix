subdir('bash')
subdir('fish')
subdir('zsh')

if host_machine.system() == 'linux'
  subdir('systemd')
endif

if host_machine.system() == 'darwin'
  subdir('launchd')
endif

if host_machine.system() == 'freebsd'
  subdir('freebsd')
endif
