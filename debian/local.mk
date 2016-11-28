dist-files += \
  $(d)/changelog \
  $(d)/prerm \
  $(d)/postrm \
  $(d)/postinst

$(eval $(call copy-file, $(d)/../scripts/post-install-linux.sh, $(d)/postinst))
$(eval $(call copy-file, $(d)/../scripts/pre-remove-linux.sh, $(d)/prerm))
$(eval $(call copy-file, $(d)/../scripts/post-remove-linux.sh, $(d)/postrm))
