# Crypted Root NixOS

## Setup

- Create a VM, 256 G disk
- Add NixOS ISO as CD-ROM and boot

- Configure disks:

  ```sh
  nixos$ sudo -s
  nixos# fdisk -l

  # Partition  Size   Type
  # /dev/sda1: 512M   EFI System
  # /dev/sda2: rest   Linux filesystem

  nixos# mkfs.fat -F32 /dev/sda1 -n 'NixOS-Boot'

  nixos# cryptsetup luksFormat --label='NixOS-Crypt' /dev/sda2

  nixos# cryptsetup luksOpen /dev/sda2 chamber
  nixos# pvcreate /dev/mapper/chamber
  nixos# vgcreate chamber /dev/mapper/chamber

  nixos# lvcreate --size 2G chamber --name swap
  nixos# lvcreate -l '100%FREE' chamber --name root

  nixos# mkswap -L 'NixOS-Swap' /dev/chamber/swap
  nixos# mkfs.xfs -L 'NixOS-Root' /dev/chamber/root
  ```

- Prepare system layout:

  ```sh
  nixos# swapon /dev/chamber/swap
  nixos# mount /dev/chamber/root /mnt

  nixos# mkdir /mnt/boot /mnt/efi
  nixos# mount /dev/sda1 /mnt/boot
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
  -  boot.initrd.kernelModules = [ "dm-snapshot" ];
  +  boot.initrd.kernelModules = [ "dm-snapshot" "cryptd" ];
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
  +    { device = "/dev/chamber/root";
         fsType = "xfs";
  @@ -20,4 +28,6 @@

  +  boot.initrd.luks.devices."chamber" = {
  +    device = "/dev/sda2";
  +  };
  +
     fileSystems."/efi" =
  -    { device = "/dev/disk/by-uuid/2737-7244";
  +    { device = "/dev/sda1";
         fsType = "vfat";
  @@ -33,3 +46,3 @@
     swapDevices =
  -    [ { device = "/dev/disk/by-uuid/ac4c0d26-4cb0-47b0-88cf-dd942249a07a"; }
  +    [ { device = "/dev/chamber/swap"; }
       ];
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
  +  networking.hostName = "nixos-crypt";
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
  +    sbctl
  +    tpm2-tools
  +  ];

  @@ -89,3 +94,10 @@
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


## Unlock with TPM

```sh
# Normally you could include 4+5, but frequent ‹nixos-rebuild› change these values.
systemd-cryptenroll --tpm2-device=auto --tpm2-pcrs=0+7+15:sha256=0000000000000000000000000000000000000000000000000000000000000000 --wipe-slot=tpm2 /dev/sda2
```

Reboot.
Re-run whenever necessary, i.e. UEFI image (PCR0) or Secure Boot status (PCR7) changes.

## Measured Kernel

`systemd-cryptsetup` will not measure Volume Keys unless kernel is measured as well.
For this we will go with Measured Unified Kernel Image.

Prepare a Secure Boot flake:

```sh
sbctl create-keys

mkdir -p /root/sb0
cd /root/sb0

nix flake init
vim flake.nix

mkdir -p nixos-crypt
cp /etc/nixos/*.nix nixos-crypt
```

Edit `nixos-crypt/configuration.nix` and set `system.copySystemConfiguration` to `false`.

Edit `flake.nix` and configure it according to [Lanzaboote Quick Start](https://github.com/nix-community/lanzaboote/blob/master/docs/QUICK_START.md) guide.
The final `flake.nix` follows, and can be found in this directory.

```nix
{
  description = "A SecureBoot-enabled NixOS configurations";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    lanzaboote = {
      url = "github:nix-community/lanzaboote/v0.4.3";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, lanzaboote, ... }: {
    nixosConfigurations = {
      nixos-crypt = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [
          ./nixos-crypt/configuration.nix
          lanzaboote.nixosModules.lanzaboote ({ pkgs, lib, ... }: {
            environment.systemPackages = [
              pkgs.sbctl
            ];

            boot.loader.systemd-boot.enable = lib.mkForce false;
            boot.lanzaboote = {
              enable = true;
              # https://github.com/nix-community/lanzaboote/issues/413#issuecomment-2726328517
              pkiBundle = "/etc/secureboot";
            };
          })
        ];
      };
    };
  };
}
```

Prepare kernel parameters for PCR 15 measurement. Edit `nixos-crypt/hardware-configuration.nix` and add `boot.kernelParams` as follows.
Use `UUID` from `blkid` of the `/dev/sda2`.

```nix
  boot.kernelParams = [
    "console=ttyS0,115200n8"
    "rd.luks=yes"
    "rd.luks.crypttab=no"
    "rd.luks.name=49854b41-fe14-47ee-a7e6-fa6c5c2e41f5=chamber"
    "rd.luks.options=49854b41-fe14-47ee-a7e6-fa6c5c2e41f5=tpm2-device=auto"
    "systemd.log_level=debug"
  ];
```

Build the generation and reboot.

```sh
nixos-rebuild switch --flake .#nixos-crypt
reboot
```

After reboot, verify the following:

1. `bootctl` shows `Measured UKI: yes`
2. `systemd-analyze pcrs` has no measurement in `15 system-identity`.


If that is the case, **poweroff** the VM, enable TPMSpy (if not already), and get the first measurement.


## Experiment 1: Without tpm2-measure-pcrs

```sh
auto-collect/collect -o data/crypted nixos-crypt 1 nixos-crypt{.vm,}
build/dump/dump --json data/crypted/nixos-crypt/0/tpmspy.bin > data/crypted/nixos-crypt/0/tpmspy.json
mv data/crypted/nixos-crypt/0 /data/crypted/0
```


## Enable tpm2-measure-pcrs=yes

Boot the VM and create another flake:

```sh
cp -r sb0 sb1
vim sb1/nixos-crypt/hardware-configuration.nix
```

Add the `tpm2-measure-pcrs=yes`:

```diff
-    "rd.luks.options=49854b41-fe14-47ee-a7e6-fa6c5c2e41f5=tpm2-device=auto"
+    "rd.luks.options=49854b41-fe14-47ee-a7e6-fa6c5c2e41f5=tpm2-device=auto,tpm2-measure-pcr=yes"
```

Reconfigure and **poweroff** before another measurement:

```sh
nixos-rebuild switch --flake .#nixos-crypt
poweroff
```


## Experiment 2: With tpm2-measure-pcrs

```sh
auto-collect/collect -o data/crypted nixos-crypt 1 nixos-crypt{.vm,}
build/dump/dump --json data/crypted/nixos-crypt/0/tpmspy.bin > data/crypted/nixos-crypt/0/tpmspy.json
mv data/crypted/nixos-crypt/ /data/crypted/1
```

## Done

Compare TPMSpy logs with TPM2 Event Log:


