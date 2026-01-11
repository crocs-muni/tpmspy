# Default NixOS

## Setup

- In libvirt, create a VM called `nixos-systemd` with 256 G disk.
- Add a software TPM.
- Add NixOS ISO as CD-ROM and start the VM.

- When you get NixOS terminal, configure disks:

  ```sh
  nixos$ sudo -s
  nixos# fdisk -l

  # Partition  Size   Type
  # /dev/vda1: 512M   EFI System
  # /dev/vda2: rest   Linux filesystem

  nixos# mkfs.fat -F32 /dev/vda1 -n 'NixOS-Boot'
  nixos# mkfs.xfs -L 'NixOS-Root' /dev/vda2
  ```

- Prepare system layout:

  ```sh
  nixos# mount /dev/vda2 /mnt

  nixos# mkdir /mnt/boot /mnt/efi
  nixos# mount /dev/vda1 /mnt/boot
  nixos# mkdir -p /mnt/efi/EFI/nixos
  nixos# mount -o bind /mnt/efi/EFI/nixos /mnt/boot
  ```

- Configure system:

  ```sh
  nixos# nixos-generate-config --root /mnt
  nixos# vim /mnt/etc/nixos/hardware-configuration.nix
  ```

  ```diff
  --- hardware-configuration.nix.orig     2025-11-12 18:50:38.015508974 +0100
  +++ hardware-configuration.nix  2025-11-13 19:48:58.491867787 +0100
  @@ -11,3 +11,5 @@
     boot.initrd.availableKernelModules = [ "ahci" "xhci_pci" "virtio_pci" "sd_mod" "sr_mod" ];
     boot.initrd.kernelModules = [ "dm-snapshot" ];
  +  boot.initrd.systemd.enable = true;
  +  boot.kernelParams = [ "console=ttyS0,115200n8" ];
     boot.kernelModules = [ "kvm-amd" ];
  @@ -15,4 +17,10 @@

  +  boot.loader.grub.extraConfig = "
  +    serial --speed=115200 --unit=0 --word=8 --parity=no --stop=1
  +    terminal_input serial
  +    terminal_output serial
  +  ";
  +
     fileSystems."/" =
  -    { device = "/dev/disk/by-uuid/3d73c064-1b31-4ba2-99be-d8b57bcab92d";
  +    { device = "/dev/vda2";
         fsType = "xfs";
  @@ -20,4 +28,6 @@

     fileSystems."/efi" =
  -    { device = "/dev/disk/by-uuid/2737-7244";
  +    { device = "/dev/vda1";
         fsType = "vfat";
  ```

  ```sh
  nixos# vim /mnt/etc/nixos/configuration.nix
  ```

  ```diff
  --- configuration.nix.orig      2025-11-12 18:50:34.528529877 +0100
  +++ configuration.nix   2025-11-13 18:37:36.154742595 +0100
  @@ -12,2 +12,4 @@

  +  nix.settings.experimental-features = [ "nix-command" "flakes" ];
  +
     # Use the systemd-boot EFI boot loader.
  @@ -15,4 +17,5 @@
     boot.loader.efi.canTouchEfiVariables = true;
  +  boot.loader.efi.efiSysMountPoint = "/efi";

  -  # networking.hostName = "nixos"; # Define your hostname.
  +  networking.hostName = "nixos-systemd";
     # Pick only one of the below networking options.
  @@ -22,3 +25,3 @@
     # Set your time zone.
  -  # time.timeZone = "Europe/Amsterdam";
  +  time.timeZone = "Europe/Prague";

  @@ -30,7 +33,7 @@
     # i18n.defaultLocale = "en_US.UTF-8";
  -  # console = {
  -  #   font = "Lat2-Terminus16";
  +  console = {
  +    font = "Lat2-Terminus16";
     #   keyMap = "us";
  -  #   useXkbConfig = true; # use xkb.options in tty.
  -  # };
  +    useXkbConfig = true; # use xkb.options in tty.
  +  };

  @@ -61,9 +64,9 @@
     # Define a user account. Don't forget to set a password with ‘passwd’.
  -  # users.users.alice = {
  -  #   isNormalUser = true;
  -  #   extraGroups = [ "wheel" ]; # Enable ‘sudo’ for the user.
  -  #   packages = with pkgs; [
  -  #     tree
  -  #   ];
  -  # };
  +  users.users.pazuzu = {
  +    isNormalUser = true;
  +    extraGroups = [ "wheel" ]; # Enable ‘sudo’ for the user.
  +    packages = with pkgs; [
  +      tree
  +    ];
  +  };

  @@ -73,6 +76,7 @@
     # $ nix search wget
  -  # environment.systemPackages = with pkgs; [
  -  #   vim # Do not forget to add an editor to edit configuration.nix! The Nano editor is also installed by default.
  -  #   wget
  -  # ];
  +  environment.systemPackages = with pkgs; [
  +    vim
  +    wget
  +    tpm2-tools
  +  ];

  @@ -89,3 +93,10 @@
     # Enable the OpenSSH daemon.
  -  # services.openssh.enable = true;
  +  services.openssh.enable = true;
  +
  +  # Enable serial console.
  +  systemd.services."getty@ttyS0" = {
  +    enable = true;
  +    wantedBy = [ "getty.target" ];
  +    serviceConfig.restart = "always";
  +  };

  @@ -100,3 +112,3 @@
     # accidentally delete configuration.nix.
  -  # system.copySystemConfiguration = true;
  +  system.copySystemConfiguration = true;
  ```

- Install system

  ```sh
  nixos# nixos-install
  …
  setting root password...
  New password:

  nixos# nixos-enter --root /mnt -c 'passwd pazuzu'
  ```

- Remove ISO, start VM.
- Ensure the system works.


## Set up SSH

Create a SSH key in `ssh` directory.
Log in to the machine using console and add the **public** part of the key to `/root/.ssh/authorized_keys` (create the path if it does not exist yet).
Ensure you can log into the machine as `root` using the key.


## Set up flakes

In Guest, create `/root/flakes`.
From Host, copy `nix/nixos-systemd/systemd.nix` to Guest `/root/flakes/systemd/flake.nix`, and `nix/nixos-systemd/systemd.d` to `/root/flakes/systemd/cfg` (`systemd.d` gets renamed to `cfg`).
Alternatively, you may add the ssh key to `install-%` rule in `nix/nixos-systemd/Makefile` and run `make install-systemd` from that directory.

Do the same for `systemd-notpm` flakes and configuration, or `make install-systemd-notpm`.
