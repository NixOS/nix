
# Backporting

Backports are handled by the backport action.

Since GitHub Actions can not trigger actions, the backport PR needs to be re-triggered by another actor. This is achieved by closing and reopening the backport PR.

This specifically affects the `installer_test` check, but note that it only runs after the other tests, so it may take a while to appear.
