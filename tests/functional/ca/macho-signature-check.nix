with import ./config.nix;

# Content-addressed derivation whose signed binary embeds its own
# output path. On a CA build the final (content-addressed) hash is
# only known after the build, so the daemon must rewrite the
# self-reference on every cold build — invalidating the signature's
# page hashes. This is the trigger `macho-signature-rewrite-check`
# refuses without any store state at all.

mkDerivation {
  __contentAddressed = true;
  outputHashMode = "recursive";
  outputHashAlgo = "sha256";
  name = "macho-signature-check-ca";
  selfPlaceholder = "${builtins.placeholder "out"}/bin/hello-self";
  buildCommand = ''
    cat > self.c <<'EOF'
    #include <stdio.h>
    int main(void) {
        printf("self=%s\n", SELF_PATH);
        return 0;
    }
    EOF
    /usr/bin/cc -O0 -Wl,-adhoc_codesign -DSELF_PATH="\"$selfPlaceholder\"" -o hello-self self.c

    mkdir -p "$out/bin"
    cp hello-self "$out/bin/hello-self"
  '';
}
