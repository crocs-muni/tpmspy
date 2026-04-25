# Fedora Workstation

## Installation

- Download Fedora Workstation ISO
- Create VM
  * Name: `fedora.vm`
  * Storage: 40 GiB
  * 8 VCPU
  * 16 GiB RAM
  * **Ensure UEFI mode**
- Install from ISO
  * Enable encryption

## Setup

- Set hostname to something like `fedora.vm`.
- Install ssh key to `/root/.ssh/authorized_keys`.
- Set up and enable `sshd.service`.

## Secure Boot

```sh
dnf install systemd-boot-unsigned systemd-ukify sbsigntools
bootctl install

cat <EOF >>/etc/systemd/ukify.conf
[UKI]
Cmdline=root=UUID=f2ed7dfd-a3eb-4136-8615-10f142d9e021 ro rootflags=subvol=root rd.luks.uuid=luks-5d06f932-8936-41c2-b680-4f1b798d9e89 rhgb quiet
EOF

mkdir /boot/efi/EFI/Linux
ukify build --linux /usr/lib/modules/$(uname -r)/vmlinuz --initrd /boot/initramfs-$(uname -r).img --output /boot/efi/EFI/Linux/fedora-$(uname -r).efi
```

Verify Secure Boot is enabled:

```sh
mokutil --sb-state
```

Create Secure Boot keys (use of RSA 2048 as recommended for compatibility):

```sh
openssl req -new -x509 -newkey rsa:2048 -keyout private.pem -out certificate.der -outform DER -nodes -subj "/CN=fedora.vm"
openssl x509 -inform der -in certificate.der -out certificate.pem
```

Sign systemd-boot:

```sh
sbsign --key /root/mok/private.pem --cert /root/mok/certificate.pem /usr/lib/systemd/boot/efi/systemd-bootx64.efi
bootctl install --secure-boot-auto-enroll=yes --certificate=/root/mok/certificate.pem --private-key=/root/mok/private.pem
```

Finally, sign the kernel and replace the original.

```sh
sbsign --key /root/mok/private.pem --cert /root/mok/certificate.pem /boot/efi/EFI/Linux/fedora-$(uname -r).efi -o /boot/efi/EFI/Linux/fedora-$(uname -r).efi
```

If that did not work, copy `/root/mok/certificate.der` (**DER**!) to `/boot` or somewhere UEFI can reach.
Then deploy this as a DB key in UEFI.
You may force reboot to firmware with `bootctl reboot-to-firmware true`.
