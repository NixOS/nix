let path = ./. + "/setPathName@invalid-name";
in "${builtins.setPathName path "valid-name"}"
