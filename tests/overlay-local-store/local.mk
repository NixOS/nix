overlay-local-store-tests := \
  $(d)/check-post-init.sh \
  $(d)/redundant-add.sh \
  $(d)/build.sh \
  $(d)/bad-uris.sh \
  $(d)/add-lower.sh \
  $(d)/gc.sh \
  $(d)/verify.sh \
  $(d)/optimise.sh

install-tests-groups += overlay-local-store
