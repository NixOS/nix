# nix-channel channel file
#============================================================================

nix_channel_inc = [include_directories('.')]


# src files
#============================================================================

nix_channel_src = files(
    'nix-channel.cc')


# targets
#============================================================================
nix_channel_src += custom_target(
  'unpack-channel.nix.gen.hh',
  output : 'unpack-channel.nix.gen.hh',
  input : 'unpack-channel.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
