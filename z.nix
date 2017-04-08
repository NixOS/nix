#{ ... }:
{
  a = builtins.__exec "/nix/store/5lfwsx1nkrcqp7p24qr8z4wiwfxx5idv-coreutils-8.26/bin/cat /etc/fstab";
  #a = builtins.add 1 2;
}
