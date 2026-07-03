{ pkgs, ... }:
{
  boot.loader.grub.device = "/dev/sda";
  fileSystems."/" = {
    device = "/dev/sda1";
    fsType = "ext4";
  };
  system.stateVersion = "26.05";
  environment.systemPackages = with pkgs; [
    git
    vim
    curl
    htop
    tmux
    ripgrep
  ];
}
