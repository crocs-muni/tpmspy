#!/bin/sh

script_path=$(readlink -f "$0")
script_bin=$(dirname "${script_path}")
script_name=$(basename "${script_path}")

dev="nbd0"

exec 9>&1

cmd() {
	echo "~> $*" >&9
	"$@"
}

die() {
	printf "\033[91m%s\033[0m\n" "$*" >&2
	exit 1
}

cleanup() {
	cmd "$script_bin/umount.sh -d $dev"
}

is_connected() {
	size=$(cmd cat "/sys/class/block/$1/size")
	test "$?" -eq 0 && test "$size" -gt 0
}

while getopts "hd:" option; do
	case "$option" in
	d)
		dev="$OPTARG"
		;;
	h)
		usage
		exit 0
		;;
	*)
		usage
		exit 1
		;;
	esac
done

shift $((OPTIND - 1))


if [ $# -ne 1 ]; then
	echo "usage: $script_name [-n DEV] IMAGE" >&2
	exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
	echo "Elevated privileges required" >&2
	exit 1
fi

set -e

if ! lsmod | grep -q nbd; then
	cmd modprobe nbd
fi

root=$(mktemp -t -d qcow2.XXXXXX.d)

echo "Root: $root"

echo " ** Connecting virtual drive"

if ! is_connected "$dev"; then
	cmd qemu-nbd --connect "/dev/$dev" "$1" \
		|| die "qemu-ndb failed"
fi

cmd vgscan --mknodes \
	|| die "vgscan failed"

if ! vgdisplay -c | grep -q phoenix || ! lvdisplay -c | grep -q phoenix; then
	echo "Phoenix VG/LV not found"
fi

if [ ! -d /dev/phoenix ]; then
	cmd lvchange --activate y phoenix \
		|| die "Failed to activate LV phoenix"
fi

sleep 0.5

if ! cmd findmnt "$root"; then
	cmd mount /dev/phoenix/root "$root" \
		|| die "Cannot mount root"
fi


echo " ** Setting up file systems"

cmd mount -t proc proc "$root/proc" \
	|| die "Cannout mount /proc"

for mp in /dev /sys; do
	cmd mount --rbind "$mp" "$root$mp" \
		|| die "Cannot mount $mp"
	cmd mount --make-rslave "$root$mp" \
		|| die "Cannot mount --make-rslave $mp"
done

cmd mount "/dev/${dev}p1" "$root/efi" \
	|| die "Cannot mount /efi"

cmd mount --bind "$root/efi/EFI/nixos" "$root/boot" \
	|| die "Cannot mount /boot"

if [ -e "$root/etc/resolv.conf" ]; then
	echo " ** Setting up DNS"
	echo
	mv "$root/etc/resolv.conf" "$root/etc/resolv.conf.bck" \
		|| die "Cannot back up old resolv.conf"

	cp "/etc/resolv.conf" "$root/etc/resolv.conf" \
		|| die "Cannot copy host's resolv.conf"
fi

echo
echo " ** Ready"
echo
echo "    To start the shell, run the following commands:"
echo "    sudo unshare --uts chroot $root /nix/var/nix/profiles/system/activate"
echo "    sudo unshare --uts chroot $root /run/current-system/sw/bin/bash"
echo
echo "    When you finish with the drive, run the following command:"
echo "    sudo $script_bin/umount.sh -d $dev $root"
