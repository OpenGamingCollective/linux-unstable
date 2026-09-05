#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# QEMU boot smoke test for the linux-unstable-ogc release builds.
#
# Boots a freshly built, *packaged* kernel image under QEMU with a tiny
# hand-rolled initramfs whose only job is to prove the kernel can
# decompress, run its initcalls and hand control to userspace: the
# embedded PID 1 prints a BOOT_OK marker on the serial console and then
# powers the VM off, which makes QEMU exit by itself (a successful boot
# never has to wait for the timeout).
#
# A boot matrix is exercised, because handhelds/gaming devices ship all
# sorts of firmware:
#
#   bios-pc   SeaBIOS (BIOS) + i440fx "pc" machine, kernel loaded by the
#             firmware's Linux loader (fw_cfg).
#   uefi-q35  OVMF (UEFI) + q35 machine, kernel booted through the EFI
#             stub (this is what Steam Deck / ROG Ally / Legion Go /
#             Ayaneo class devices actually boot from).
#
# Every boot also attaches a spread of boot-relevant PCI devices (ICH9
# AHCI comes with q35, PIIX IDE with pc, plus NVMe, virtio-blk,
# virtio-scsi, e1000e and xHCI USB) so as many storage/USB/net boot
# drivers as the built kernel contains get enumerated and probed.
#
# Each distro job (Arch/Fedora/Debian) runs this against the exact image
# it is about to upload, before uploading it. The release job only runs
# when every distro job passes, so an unbootable kernel can never be
# published.
#
# Usage:   boot-smoke-test.sh <bzImage>
# Env:     KREL  expected kernel release string; when set, the boot banner
#                "Linux version <KREL>" must appear on every console log.
#          BOOT_SMOKE_TIMEOUT  override the per-boot timeout in seconds
#                              (default: 300 under KVM, 1200 under TCG).
# The kernel command line pins loglevel=7: some distro configs default the
# console to a quieter level (Arch ships CONSOLE_LOGLEVEL_DEFAULT=4), which
# would suppress the KERN_NOTICE boot banner and make the banner check below
# fail on an otherwise perfectly bootable kernel.
# Requires (installed by the calling CI job): qemu-system-x86_64, cpio,
#          gzip, a C compiler (gcc or clang), an OVMF build for the UEFI
#          leg (packages: ovmf / edk2-ovmf), coreutils (timeout, find).

set -euo pipefail

die() { echo "::error::$*" >&2; exit 1; }

IMAGE="${1:?usage: boot-smoke-test.sh <bzImage>}"
[ -s "$IMAGE" ] || die "kernel image '$IMAGE' not found or empty"

for tool in qemu-system-x86_64 cpio gzip timeout sha256sum find truncate; do
	command -v "$tool" >/dev/null || die "required tool '$tool' not installed"
done
CC_BIN="$(command -v gcc || command -v clang || true)"
[ -n "$CC_BIN" ] || die "no C compiler (gcc or clang) found"

WORK="$(mktemp -d /tmp/boot-smoke.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

# ---------------------------------------------------------------------------
# PID 1 for the test initramfs: freestanding, no libc, raw x86_64 syscalls,
# so it compiles with gcc or clang on any distro without extra static-libc
# packages (the Fedora build env has no glibc-static, for example).
# ---------------------------------------------------------------------------
cat > "$WORK/init.c" <<'EOF'
/*
 * Tiny freestanding PID 1 for the QEMU boot smoke test: print BOOT_OK on
 * the serial console, then power the VM off so QEMU exits on its own.
 */
static long sys3(long nr, long a, long b, long c)
{
	long ret;

	__asm__ volatile ("syscall"
			  : "=a"(ret)
			  : "a"(nr), "D"(a), "S"(b), "d"(c)
			  : "rcx", "r11", "memory");
	return ret;
}

void _start(void)
{
	static const char msg[] =
		"BOOT_OK: linux-unstable-ogc booted to userspace init\n";
	long fd;

	/* CONFIG_DEVTMPFS_MOUNT does not apply to initramfs, so mount
	 * devtmpfs ourselves to get /dev/ttyS0. */
	sys3(83, (long)"/dev", 0755, 0);				/* mkdir	*/
	sys3(165, (long)"devtmpfs", (long)"/dev", (long)"devtmpfs");	/* mount	*/

	/* Announce success on the serial port and on stdio (fd 1 exists
	 * only when the kernel could open /dev/console from the initramfs). */
	fd = sys3(2, (long)"/dev/ttyS0", 1, 0);				/* open		*/
	if (fd >= 0)
		sys3(1, fd, (long)msg, sizeof(msg) - 1);		/* write	*/
	sys3(1, 1, (long)msg, sizeof(msg) - 1);				/* write	*/

	/* reboot(LINUX_REBOOT_CMD_POWER_OFF) -> ACPI S5 -> QEMU exits. */
	sys3(169, 0xfee1deadL, 672274793L, 0x4321fedcL);
	for (;;)
		sys3(34, 0, 0, 0);					/* pause	*/
}
EOF

mkdir -p "$WORK/initramfs/dev"
"$CC_BIN" -Os -g0 -static -no-pie -nostdlib -ffreestanding \
	-fno-stack-protector -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -Wl,-z,noexecstack \
	-o "$WORK/initramfs/init" "$WORK/init.c"
# /dev/console lets the kernel wire init's stdio to the console; creating it
# needs mknod privileges and may fail for unprivileged callers. Harmless: the
# init above then opens /dev/ttyS0 itself after mounting devtmpfs.
mknod -m 600 "$WORK/initramfs/dev/console" c 5 1 2>/dev/null || true

(cd "$WORK/initramfs" && find . -print0 | cpio --null -o -H newc --quiet) \
	| gzip -1 > "$WORK/initrd.img"

# Use KVM when the runner exposes it, software emulation (TCG) otherwise.
# TCG boots a distro-config kernel in a couple of minutes; the generous
# timeout also covers hangs, which are exactly what this test must catch.
if [ -w /dev/kvm ]; then
	ACCEL="kvm"
	ACCEL_ARGS=(-accel kvm -cpu host)
	TMO=300
else
	ACCEL="tcg"
	ACCEL_ARGS=(-accel tcg -cpu max)
	TMO=1200
fi

# ---------------------------------------------------------------------------
# Locate an OVMF build for the UEFI leg. Layouts, by distro:
#   Debian: /usr/share/OVMF/OVMF_CODE(_4M).fd      (package: ovmf)
#   Fedora: /usr/share/edk2/ovmf/OVMF_CODE.fd      (package: edk2-ovmf)
#   Arch:   /usr/share/edk2/x64/OVMF_CODE.4m.fd    (package: edk2-ovmf)
#   qemu:   /usr/share/qemu/edk2-x86_64-code.fd    (qemu-system-data)
# CODE and VARS must be the same flash size, hence the paired candidates.
# ---------------------------------------------------------------------------
OVMF_PAIR=""
for pair in \
	"/usr/share/OVMF/OVMF_CODE_4M.fd:/usr/share/OVMF/OVMF_VARS_4M.fd" \
	"/usr/share/OVMF/OVMF_CODE.fd:/usr/share/OVMF/OVMF_VARS.fd" \
	"/usr/share/edk2/x64/OVMF_CODE.4m.fd:/usr/share/edk2/x64/OVMF_VARS.4m.fd" \
	"/usr/share/edk2/x64/OVMF_CODE.fd:/usr/share/edk2/x64/OVMF_VARS.fd" \
	"/usr/share/edk2/ovmf/OVMF_CODE.fd:/usr/share/edk2/ovmf/OVMF_VARS.fd" \
	"/usr/share/ovmf/x64/OVMF_CODE.fd:/usr/share/ovmf/x64/OVMF_VARS.fd" \
	"/usr/share/qemu/edk2-x86_64-code.fd:/usr/share/qemu/edk2-x86_64-vars.fd" \
	; do
	if [ -r "${pair%%:*}" ] && [ -r "${pair##*:}" ]; then
		OVMF_PAIR="$pair"
		break
	fi
done
[ -n "$OVMF_PAIR" ] || die "no OVMF (UEFI) firmware found; install the 'ovmf' (Debian) or 'edk2-ovmf' (Arch/Fedora) package"
OVMF_CODE="${OVMF_PAIR%%:*}"
cp "${OVMF_PAIR##*:}" "$WORK/OVMF_VARS.fd"	# pflash needs a writable copy
echo "UEFI firmware: $OVMF_CODE"

# Blank disks so the storage controllers have something to enumerate.
for d in nvme virtio scsi; do
	truncate -s 64M "$WORK/disk-$d.img"
done

# Device spread common to every boot: covers the storage/USB/net drivers a
# gaming kernel is most likely to boot from. Attached whether or not the
# kernel contains them; drivers that are modules are simply not probed,
# which costs nothing.
DRIVERS_ARGS=(
	-drive if=none,id=dsk-nvme,format=raw,file="$WORK/disk-nvme.img"
	-device nvme,drive=dsk-nvme,serial=ogcsmoke
	-drive if=none,id=dsk-virtio,format=raw,file="$WORK/disk-virtio.img"
	-device virtio-blk-pci,drive=dsk-virtio
	-drive if=none,id=dsk-scsi,format=raw,file="$WORK/disk-scsi.img"
	-device virtio-scsi-pci,id=scsi0
	-device scsi-hd,drive=dsk-scsi
	-device e1000e,netdev=net0 -netdev user,id=net0,restrict=on
	-device qemu-xhci -device usb-tablet
)

# run_boot <name> <machine args...> — boot, wait for QEMU to exit or the
# timeout to fire, then fail hard if the marker (or the expected release
# banner) is missing from the serial console log.
run_boot() {
	local name="$1"; shift
	local serial="$WORK/serial-$name.log"
	: > "$serial"

	echo "[$name] Booting $(basename "$IMAGE") (sha256 $(sha256sum "$IMAGE" | cut -c1-16)...) with QEMU ($ACCEL, timeout ${TMO}s)"
	timeout "$TMO" qemu-system-x86_64 "${ACCEL_ARGS[@]}" "$@" \
		"${DRIVERS_ARGS[@]}" \
		-m 2048 -smp 2 -nodefaults \
		-display none -monitor none -no-reboot \
		-serial "file:$serial" \
		-kernel "$IMAGE" -initrd "$WORK/initrd.img" \
		-append "console=ttyS0,115200n8 rdinit=/init panic=-1 nokaslr loglevel=7" \
		|| true
	# A panic with panic=-1 reboots instantly and -no-reboot makes QEMU
	# exit, so both "qemu exited by itself" and "timeout killed it" end up
	# in the marker check below.

	if ! grep -q "BOOT_OK" "$serial"; then
		echo "::error::QEMU boot smoke test FAILED in the '$name' configuration: no BOOT_OK marker on the serial console (accel=$ACCEL, timeout=${TMO}s). The kernel is unbootable."
		echo "----- last 250 lines of the $name guest serial console -----"
		tail -n 250 "$serial" || true
		echo "-------------------------------------------------------------"
		exit 1
	fi
	if [ -n "${KREL:-}" ] && ! grep -qF "Linux version ${KREL} " "$serial"; then
		echo "::error::QEMU boot smoke test FAILED in the '$name' configuration: the booted kernel banner does not advertise release '${KREL}'."
		# Show what the console actually carried: log lines carry a
		# "[    0.000000] " timestamp prefix, so anchor-free context of
		# the early console output is what makes this diagnosable.
		echo "----- first 25 lines of the $name guest serial console -----"
		head -n 25 "$serial" | sed 's/\r$//'
		echo "-------------------------------------------------------------"
		exit 1
	fi
	echo "[$name] Boot smoke test PASSED: kernel booted to userspace init and powered off. Serial console tail:"
	tail -n 10 "$serial" || true
}

# 1. Classic BIOS boot: SeaBIOS firmware, i440fx (PIIX IDE built in).
run_boot bios-pc -machine pc

# 2. UEFI boot: OVMF firmware, q35 (ICH9 AHCI + PCIe built in), kernel
#    launched through the EFI stub — the path handhelds actually use.
run_boot uefi-q35 \
	-machine q35 \
	-drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
	-drive if=pflash,format=raw,file="$WORK/OVMF_VARS.fd"

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
	{
		echo '### QEMU boot smoke test'
		echo
		echo "- Image: $(basename "$IMAGE") (sha256 $(sha256sum "$IMAGE" | cut -c1-12)...)"
		echo "- Accelerator: ${ACCEL}, timeout ${TMO}s per boot"
		echo "- Matrix: bios-pc (SeaBIOS) and uefi-q35 (OVMF EFI stub), NVMe + virtio-blk + virtio-scsi + e1000e + xHCI attached"
		echo '- Result: BOOT_OK in both configurations - kernel decompressed, booted and reached userspace init'
	} >> "$GITHUB_STEP_SUMMARY"
fi
