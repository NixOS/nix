local-overlay-store-tests := \
  $(d)/check-post-init.sh \
  $(d)/redundant-add.sh \
  $(d)/build.sh \
  $(d)/bad-uris.sh \
  $(d)/add-lower.sh \
  $(d)/delete-refs.sh \
  $(d)/delete-duplicate.sh \
  $(d)/gc.sh \
  $(d)/verify.sh \
  $(d)/optimise.sh \
  $(d)/stale-file-handle.sh

install-tests-groups += local-overlay-store
