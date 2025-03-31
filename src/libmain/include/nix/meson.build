# Public headers directory

include_dirs = [include_directories('..')]

config_h = configure_file(
  configuration : configdata,
  output : 'config-main.hh',
)

headers = [config_h] + files(
  'common-args.hh',
  'loggers.hh',
  'plugin.hh',
  'progress-bar.hh',
  'shared.hh',
)
