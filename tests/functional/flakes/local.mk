flake-tests := \
  $(d)/flakes.sh \
  $(d)/develop.sh \
  $(d)/edit.sh \
  $(d)/run.sh \
  $(d)/mercurial.sh \
  $(d)/circular.sh \
  $(d)/init.sh \
  $(d)/inputs.sh \
  $(d)/follow-paths.sh \
  $(d)/bundle.sh \
  $(d)/check.sh \
  $(d)/unlocked-override.sh \
  $(d)/absolute-paths.sh \
  $(d)/absolute-attr-paths.sh \
  $(d)/build-paths.sh \
  $(d)/flake-in-submodule.sh \
  $(d)/prefetch.sh \
  $(d)/eval-cache.sh \
  $(d)/search-root.sh \
  $(d)/config.sh \
  $(d)/show.sh \
  $(d)/dubious-query.sh

install-tests-groups += flake
