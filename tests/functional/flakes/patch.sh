source common.sh

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2
flake3Dir=$TEST_ROOT/flake3

rm -rf $flake1Dir $flake2Dir $flake3Dir
mkdir -p $flake1Dir/dir $flake2Dir $flake3Dir

cat > $flake2Dir/flake.nix <<EOF
{
  outputs = { self }: {
    y = 2;
    z = import ./z.nix;
  };
}
EOF

echo "10 + 20" > $flake2Dir/z.nix

cat > $flake1Dir/dir/flake.nix <<EOF
{
  inputs.flake2 = {
    url = path://$flake2Dir?lock=1;
    patchFiles = [ "../p1.patch" "../p2.patch" ./p3.patch ];
  };

  outputs = { self, flake2 }: {
    x = 1 + flake2.y + flake2.z;
  };
}
EOF

cat > $flake1Dir/p1.patch <<EOF
diff -ru a/flake.nix b/flake.nix
--- a/flake.nix	2023-03-24 13:36:31.152372459 +0100
+++ b/flake.nix	2023-03-24 13:35:59.309846802 +0100
@@ -1,6 +1,6 @@
 {
   outputs = { self }: {
-    y = 2;
+    y = 22;
     z = import ./z.nix;
   };
 }
EOF

cat > $flake1Dir/p2.patch <<EOF
diff -ru a/z.nix b/z.nix
--- a/z.nix	2023-03-24 13:36:31.152372459 +0100
+++ b/z.nix	2023-03-24 13:36:12.900071116 +0100
@@ -1 +1 @@
-10 + 20
+100 + 200
EOF

cat > $flake1Dir/dir/p3.patch <<EOF
diff -ru a/z.nix b/z.nix
--- a/z.nix	2023-03-24 13:36:31.152372459 +0100
+++ b/z.nix	2023-03-24 13:36:12.900071116 +0100
@@ -1 +1 @@
-100 + 200
+111 + 222
EOF

# Do this twice. First one will apply the patches in flake.cc, the
# second in call-flake.nix.
[[ $(nix eval -vvvvv path://$flake1Dir?dir=dir#x) = 356 ]]
[[ $(nix eval path://$flake1Dir?dir=dir#x) = 356 ]]

cat > $flake3Dir/flake.nix <<EOF
{
  inputs.flake1 = {
    url = path://$flake1Dir?dir=dir&lock=1;
  };

  outputs = { self, flake1 }: {
    a = flake1.x * 2;
  };
}
EOF

[[ $(nix eval $flake3Dir#a) = 712 ]]
rm $flake1Dir/dir/flake.lock $flake3Dir/flake.lock
[[ $(nix eval $flake3Dir#a) = 712 ]]
[[ $(nix eval $flake3Dir#a) = 712 ]]
