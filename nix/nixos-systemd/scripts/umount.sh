#!/bin/sh -x

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
	echo "usage: $script_name [-n DEV] ROOT" >&2
	exit 1
fi

root="$1"

echo " ** Restoring modifications"

if [ -f "$root/etc/resolv.conf" ] && [ -e "$root/etc/resolv.conf.bck" ]; then
	unlink "$root/etc/resolv.conf"
	mv "$root/etc/resolv.conf.bck" "$root/etc/resolv.conf" \
		|| die "Cannot restore resolv.conf"
fi

echo " ** Unmounting disk"

attempt=1
while findmnt "$root" >/dev/null && [ "$attempt" -le 3 ]; do
	attempt=$((attempt + 1))
	cmd umount -Rf "$root" \
		|| die "Cannot unmount root"
	sleep 0.5
done

# Doing 'lvchange' before this command can, in some rare circumstances,
# report the volume as still being used.
cmd rmdir "$root"

cmd lvchange --activate n phoenix \
	|| die "Cannot deactivate LV phoenix"

cmd qemu-nbd --disconnect "/dev/$dev" \
	|| die "Cannot disconnect $dev"

echo " ** Done"
