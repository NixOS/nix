sources += files(
  'spotlight-apps.cc',
)

# Frameworks needed by the hybrid app-bundle materializer. CoreFoundation is
# already pulled in transitively by Apple's libSystem on darwin, but listing
# it explicitly here documents the dependency. CoreServices gives us
# LSRegisterURL.
deps_other += [
  dependency('appleframeworks', modules : [ 'CoreFoundation', 'CoreServices' ]),
]

subdir('include/nix/store')
