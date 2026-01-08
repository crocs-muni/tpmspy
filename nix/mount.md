# How to edit a file in a QCOW2 disk

1. Mount the partition

    modprobe nbd max_part=2
    qemu-nbd --connect /dev/nbd0 "$PATH_TO_QCOW2_IMAGE"

2. Find the partition to mount

    fdisk -l /dev/nbd0

3. Mount the partition

    mount /dev/nbd0p"$PARTITION" "$MOUNT_POINT"

4. Browse and edit files in `$MOUNT_POINT`.

   It may be useful to use `chroot`:

    chroot "$MOUNT_POINT" /bin/bash

   If `/bin/bash` is omitted, `$SHELL` will be supplied, which does not need
   to exist in the virtual machine (e.g. zsh or fish).

5. Disconnect the device

    umount $MOUNT_POINT

    # If the device is a part of LVM, deactivate the volume group first!
    vgchange --activate n $VG

    qemu-nbd --disconnect /dev/nbd0
    rmmod nbd
