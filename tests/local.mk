check:
    @echo "Warning: Nix has no 'make check'. Please install Nix and run 'make installcheck' instead."

ifeq (MINGW,$(findstring MINGW,$(OS)))

# these do not pass (yet) on MINGW:

#  gc-concurrent.sh         <-- fails trying to delete open .lock-file, need runtimeRoots
#  gc-runtime.sh            <-- runtimeRoots not implemented yet
#  user-envs.sh             <-- nix-env is not implemented yet
#  remote-store.sh          <-- not implemented yet
#  secure-drv-outputs.sh    <-- needs nix-daemin which is not ported yet
#  nix-channel.sh           <-- nix-channel is not implemented yet
#  nix-profile.sh           <-- not implemented yet
#  case-hack.sh             <-- not implemented yet (it might have Windows-specific)
#  nix-shell.sh             <-- not implemented yet
#  linux-sandbox.sh         <-- not implemented (Docker can be use on Windows for sandboxing)
#  plugins.sh               <-- not implemented yet
#  nix-copy-ssh.sh          <-- not implemented yet
#  build-remote.sh          <-- not implemented yet
#  binary-cache.sh          <-- \* does not work in MSYS Bash (https://superuser.com/questions/897599/escaping-asterisk-in-bash-on-windows)
#  nar-access.sh            <-- not possible to have args '/foo/data' (paths inside nar) without magic msys path translation (maybe `bash -c '...'` will work?)
endif

nix_tests = \
  init.sh hash.sh lang.sh add.sh simple.sh dependencies.sh \
  gc.sh gc-concurrent.sh \
  referrers.sh user-envs.sh logging.sh nix-build.sh misc.sh fixed.sh \
  gc-runtime.sh check-refs.sh filter-source.sh \
  remote-store.sh export.sh export-graph.sh \
  timeout.sh secure-drv-outputs.sh nix-channel.sh \
  multiple-outputs.sh import-derivation.sh fetchurl.sh optimise-store.sh \
  binary-cache.sh nix-profile.sh repair.sh dump-db.sh case-hack.sh \
  check-reqs.sh pass-as-file.sh tarball.sh restricted.sh \
  placeholders.sh nix-shell.sh \
  linux-sandbox.sh \
  build-dry.sh \
  build-remote.sh \
  nar-access.sh \
  structured-attrs.sh \
  fetchGit.sh \
  fetchMercurial.sh \
  signing.sh \
  run.sh \
  brotli.sh \
  pure-eval.sh \
  check.sh \
  plugins.sh \
  search.sh \
  nix-copy-ssh.sh
  # parallel.sh

install-tests += $(foreach x, $(nix_tests), tests/$(x))

tests-environment = NIX_REMOTE= $(bash) -e

clean-files += $(d)/common.sh

ifeq (MINGW,$(findstring MINGW,$(OS)))
installcheck: $(d)/common.sh
else
installcheck: $(d)/common.sh $(d)/plugins/libplugintest.$(SO_EXT)
endif
