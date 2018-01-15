#!/bin/sh
set -ex

substr() { [ -z "${2##*$1*}" ]; }

#apt-get install qemu qemu-user-static qemu-system-arm debootstrap fakeroot proot
mychroot_nocwd() {
        # LC_ALL + LANGUAGE set to avoid lots of print errors due to locale not being set inside container
        # PATH is needed to be able to reach binaries like ldconfig without logging in to root, which adds the paths to PATH.
        # PROOT_NO_SECCOMP is requried due to proot bug #106
        LC_ALL=C LANGUAGE=C PATH="$PATH:/usr/sbin:/sbin" PROOT_NO_SECCOMP=1 proot -r "$ROOTFS" -w / -b /proc --root-id -q qemu-arm-static "$@"
}

mychroot() {
        mychroot_nocwd -w / "$@"
}

if [ -z "${INSIDE_CHROOT}" ]; then

        osmo-clean-workspace.sh

        # Only use ARM chroot if host is not ARM and the target is ARM:
        if ! $(substr "arm" "$(uname -m)") && [ "x${INSTR}" = "x--with-neon" -o "x${INSTR}" = "x--with-neon-vfpv4" ]; then

                OSMOTRX_DIR="$PWD" # we assume we are called as contrib/jenkins.sh
                ROOTFS_PREFIX="${ROOTFS_PREFIX:-/opt}"
                ROOTFS="${ROOTFS_PREFIX}/qemu-img"
                mkdir -p "${ROOTFS_PREFIX}"

                # Prepare chroot:
                if [ ! -d "$ROOTFS" ]; then
                        mkdir -p "$ROOTFS"
                        if [ "x${USE_DEBOOTSTRAP}" = "x1" ]; then
                                fakeroot qemu-debootstrap --foreign --include="linux-image-armmp-lpae" --arch=armhf stretch "$ROOTFS" http://ftp.de.debian.org/debian/
                                # Hack to avoid debootstrap trying to mount /proc, as it will fail with "no permissions" and anyway proot takes care of it:
                                sed -i "s/setup_proc//g" "$ROOTFS/debootstrap/suite-script"
                                mychroot /debootstrap/debootstrap --second-stage --verbose http://ftp.de.debian.org/debian/
                        else
                                wget -nc -q "https://uk.images.linuxcontainers.org/images/debian/stretch/armhf/default/20180114_22:42/rootfs.tar.xz"
                                tar -xf rootfs.tar.xz -C "$ROOTFS/" || true
                                echo "nameserver 8.8.8.8" > "$ROOTFS/etc/resolv.conf"
                        fi
                        mychroot -b /dev apt-get update
                        mychroot apt-get -y install build-essential dh-autoreconf pkg-config libuhd-dev libusb-1.0-0-dev libusb-dev git
                fi
                # Run jenkins.sh inside the chroot:
                INSIDE_CHROOT=1 mychroot_nocwd -w /osmo-trx -b "$OSMOTRX_DIR:/osmo-trx" -b "$(which osmo-clean-workspace.sh):/usr/bin/osmo-clean-workspace.sh" ./contrib/jenkins.sh
                exit 0
        fi
fi

### BUILD osmo-trx

autoreconf --install --force
./configure $INSTR
$MAKE $PARALLEL_MAKE
$MAKE check \
  || cat-testlogs.sh

if [ -z "x${INSIDE_CHROOT}" ]; then
        osmo-clean-workspace.sh
fi
