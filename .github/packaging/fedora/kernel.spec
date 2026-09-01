# SPDX-License-Identifier: GPL-2.0-only
#
# RPM spec for the linux-unstable-ogc kernel (linux-next based).
#
# Built by .github/workflows/build-kernel.yml ("fedora" job) which, before
# invoking rpmbuild:
#   - substitutes @@KBASEVER@@ / @@KVERDOTTED@@ / @@SHA8@@ placeholders below
#   - stages SOURCES/linux.tar.gz  (kernel tree contents, root dir "linux/",
#     no VCS data, localversion-next included)
#   - stages SOURCES/config        (Fedora kernel-core .config with the OGC
#                                   kernel-packages fragments and the local
#                                   config.fragment already applied)
#
# Derived from the OpenGamingCollective/kernel-packages fedora/kernel.spec
# (itself based on CachyOS/Nobara), trimmed down to core/modules/devel only.
# The kernel is compiled with clang (LLVM=1) exactly like the Arch packages.

%global _default_patch_fuzz 2

# See https://fedoraproject.org/wiki/Changes/SetBuildFlagsBuildCheck
%if 0%{?fedora} >= 37
%undefine _auto_set_build_flags
%endif

%define _build_id_links none
%define _disable_source_fetch 1
# no debuginfo generation, no brp strip/mangle of kernel binaries
%define debug_package %{nil}
%define __spec_install_post /usr/lib/rpm/brp-compress || :

# ---- substituted by CI ------------------------------------------------------
%define kbasever @@KBASEVER@@
%define sha8 @@SHA8@@
# ----------------------------------------------------------------------------

Version: @@KVERDOTTED@@
Release: 1.g%{sha8}%{?dist}

%define rpmver %{version}-%{release}
# Kernel release string (uname -r). Identical to what the localversion* files
# below produce during the build, and identical to the Arch packages built
# from the same commit:
#   <kbasever>-unstable-ogc-g<sha8>-1
%define kverstr %{kbasever}-unstable-ogc-g%{sha8}-1
# RPM dependency versions may contain at most ONE hyphen (V-R separator), so
# the full kernel release string cannot be used as a Provides: version. Use a
# 1:1 dotted translation for the *-uname-r provides instead.
%define kverdot %(echo "%{kverstr}" | sed -e "s/-/./g")

Name: kernel-unstable-ogc
Summary: The linux-next kernel for the Open Gaming Collective
License: GPLv2
URL: https://github.com/OpenGamingCollective/linux-unstable
Group: System Environment/Kernel
ExclusiveArch: x86_64
Source0: linux.tar.gz
Source1: config

BuildRequires: bash, coreutils, make, tar, findutils, gawk, diffutils, m4
BuildRequires: bc, bison, flex, perl-interpreter, perl-Carp, binutils
BuildRequires: xz, zstd, kmod, python3
BuildRequires: elfutils-libelf-devel, elfutils-devel
BuildRequires: openssl, openssl-devel
BuildRequires: dwarves, hmaccalc
# clang toolchain (like the Arch packages)
BuildRequires: clang, lld, llvm, ccache
# needed when the base config enables CONFIG_RUST. The bindgen binary
# package was renamed from rust-bindgen to bindgen in newer Fedora releases;
# accept either so this spec works across distro versions.
BuildRequires: rust, rust-src
BuildRequires: (bindgen or rust-bindgen)

# All kernel make invocations: clang via ccache, deterministic version strings
%define kmake make CC="ccache clang" LLVM=1 LLVM_IAS=1 WERROR=0 KBUILD_BUILD_HOST=ogc-ci KBUILD_BUILD_USER=kernel-unstable-ogc KBUILD_BUILD_TIMESTAMP=""

%description
This package is a meta package that pulls in the linux-unstable-ogc kernel
(a linux-next snapshot for the Open Gaming Collective) and its matching
modules.

%package core
Summary: The linux-unstable-ogc kernel (vmlinuz and core files)
Group: System Environment/Kernel
Provides: installonlypkg(kernel)
Provides: %{name}-core-uname-r = %{kverdot}
Requires: bash, coreutils, kmod
Requires: /usr/bin/kernel-install
Requires: %{name}-modules = %{rpmver}
Recommends: linux-firmware
%description core
This package contains the linux-unstable-ogc kernel image (vmlinuz),
System.map, the build configuration and the module symbol version file.

%package modules
Summary: Kernel modules to match the linux-unstable-ogc core kernel
Group: System Environment/Kernel
Provides: installonlypkg(kernel-module)
Provides: %{name}-modules-uname-r = %{kverdot}
Supplements: %{name}-core = %{rpmver}
# kmod needed for depmod in %%post
Requires: kmod
%description modules
This package provides the kernel modules for the linux-unstable-ogc kernel.

%package devel
Summary: Development files for building external modules
Group: Development/System
AutoReqProv: no
Requires: findutils, make, perl-interpreter, flex, bison
Requires: elfutils-libelf-devel, openssl-devel, gcc
Requires: clang, llvm, lld
Provides: %{name}-devel-uname-r = %{kverdot}
Enhances: akmods
Enhances: dkms
%description devel
This package provides the headers, scripts and tooling (objtool,
resolve_btfids) needed to build out-of-tree kernel modules against the
linux-unstable-ogc kernel (%{kverstr}).

%prep
%setup -q -n linux

cp %{SOURCE1} .config

# The Fedora distro config references Fedora-only certificate files that do
# not exist in this tree; use the ephemeral in-tree key instead.
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str SYSTEM_REVOCATION_KEYS ""
# CONFIG_MODULE_SIG_KEY points at the Red Hat signing cert in the distro
# config, which does not exist in this tree. Reset it to the kbuild default
# ("certs/signing_key.pem"): an ephemeral self-signed key generated during
# the build and trusted by the kernel itself. The Fedora config sets
# CONFIG_MODULE_SIG_ALL=y, and with an empty key string scripts/Makefile.modinst
# resolves sig-key to "./", making sign-file read a directory as the private
# key (SSL DECODER error) on every module.
scripts/config --set-str MODULE_SIG_KEY "certs/signing_key.pem"

# Deterministic / distro-agnostic build identity
scripts/config -u DEFAULT_HOSTNAME
scripts/config --set-str BUILD_SALT "%{kverstr}"

# Kernel release suffix, same scheme as the Arch packages:
#   <base><localversion-next>-unstable-ogc-g<sha8>-1
echo "-unstable-ogc-g%{sha8}" > localversion.10-pkgname
echo "-1" > localversion.20-pkgrel

# glibc >= 2.42 const-correctness vs -Werror in tools/lib/bpf (used by
# resolve_btfids when DEBUG_INFO_BTF=y); keep the build green.
if [ -f tools/lib/bpf/Makefile ]; then
    sed -i 's/ -Werror -Wall/ -Wall/' tools/lib/bpf/Makefile || true
fi

%{kmake} olddefconfig

# Fail fast if the release string ever drifts from the spec
REL="$(make -s kernelrelease)"
if [ "$REL" != "%{kverstr}" ]; then
    echo "kernelrelease '$REL' does not match spec kverstr '%{kverstr}'" >&2
    exit 1
fi
cp .config config-linux-unstable-ogc

%build
%{kmake} %{?_smp_mflags} all

%install
MODDIR="%{buildroot}/lib/modules/%{kverstr}"
DEVEL="%{buildroot}%{_prefix}/src/kernels/%{kverstr}"

mkdir -p "%{buildroot}/boot" "$MODDIR"

echo "Installing boot image..."
ImageName="$(make -s image_name | tail -n 1)"
install -m 0644 "$ImageName" "$MODDIR/vmlinuz"
chmod 0755 "$MODDIR/vmlinuz"

echo "Installing modules..."
# Modules are compressed by modules_install itself (CONFIG_MODULE_COMPRESS_*).
# depmod runs from %%post at install time.
%{kmake} %{?_smp_mflags} KERNELRELEASE=%{kverstr} \
    INSTALL_MOD_PATH=%{buildroot} INSTALL_MOD_STRIP=1 \
    DEPMOD=/doesnt/exist modules_install

echo "Installing core files..."
cp System.map "$MODDIR/System.map"
cp .config    "$MODDIR/config"
gzip -c9 < Module.symvers > "$MODDIR/symvers.gz"
(cd "$MODDIR" && sha512hmac vmlinuz > .vmlinuz.hmac)

# ---- kernel-devel -----------------------------------------------------------
echo "Preparing kernel-devel..."
rm -f "$MODDIR"/build "$MODDIR"/source
ln -s "%{_prefix}/src/kernels/%{kverstr}" "$MODDIR/build"
(cd "$MODDIR" && ln -s build source)
mkdir -p "$MODDIR"/updates "$MODDIR"/weak-updates "$DEVEL"

find . -type f \( -name 'Makefile*' -o -name 'Kconfig*' \) -print0 \
    | xargs -0 cp --parents -t "$DEVEL"
cp -a include "$DEVEL"/
cp -a arch/x86/include "$DEVEL"/arch/x86/
if [ -f arch/x86/kernel/module.lds ]; then
    cp -a --parents arch/x86/kernel/module.lds "$DEVEL"/
fi
cp -a scripts "$DEVEL"/
rm -rf "$DEVEL"/scripts/tracing
rm -f  "$DEVEL"/scripts/spdxcheck.py
cp Module.symvers System.map .config "$DEVEL"/

mkdir -p "$DEVEL"/tools/{objtool,bpf/resolve_btfids,lib,build}
cp -a tools/objtool/objtool                      "$DEVEL"/tools/objtool/ || :
cp -a tools/bpf/resolve_btfids/resolve_btfids    "$DEVEL"/tools/bpf/resolve_btfids/ || :
cp -a tools/include                              "$DEVEL"/tools/
cp -a tools/lib/subcmd                           "$DEVEL"/tools/lib/
cp -a tools/lib/bpf                              "$DEVEL"/tools/lib/
cp -a tools/build/Build.include tools/build/fixdep.c "$DEVEL"/tools/build/
cp -a tools/scripts/utilities.mak                "$DEVEL"/tools/scripts/ 2>/dev/null || :
cp -a --parents arch/x86/entry/syscalls/syscall_32.tbl "$DEVEL"/
cp -a --parents arch/x86/entry/syscalls/syscall_64.tbl "$DEVEL"/
cp -a arch/x86/tools                             "$DEVEL"/arch/x86/

# Drop intermediate build artifacts from devel tree
find "$DEVEL" \( -name '*.o' -o -name '*.cmd' -o -name '.*.cmd' \) -delete

# Timestamps must line up so external module builds do not rerun kconfig
touch -r "$DEVEL"/Makefile \
      "$DEVEL"/include/generated/uapi/linux/version.h \
      "$DEVEL"/include/config/auto.conf

%post core
# nothing to do at this point

%posttrans core
# Runs after ALL packages of this transaction have been installed and their
# %%post scriptlets (incl. depmod from -modules) have run.
if [ -x /usr/bin/kernel-install ]; then
    /usr/bin/kernel-install add %{kverstr} /lib/modules/%{kverstr}/vmlinuz || exit $?
fi
if [ -x /usr/sbin/grubby ]; then
    grubby --set-default="/boot/vmlinuz-%{kverstr}" || :
fi

%preun core
if [ "$1" = "0" ] && [ -x /usr/bin/kernel-install ]; then
    /usr/bin/kernel-install remove %{kverstr} /lib/modules/%{kverstr}/vmlinuz || exit $?
fi

%post modules
/sbin/depmod -a %{kverstr}

%files
# meta package: everything lives in the subpackages

%files core
%ghost /boot/vmlinuz-%{kverstr}
%ghost /boot/initramfs-%{kverstr}.img
/lib/modules/%{kverstr}/vmlinuz
/lib/modules/%{kverstr}/.vmlinuz.hmac
/lib/modules/%{kverstr}/System.map
/lib/modules/%{kverstr}/config
/lib/modules/%{kverstr}/symvers.gz

%files modules
/lib/modules/%{kverstr}/
%exclude /lib/modules/%{kverstr}/vmlinuz
%exclude /lib/modules/%{kverstr}/.vmlinuz.hmac
%exclude /lib/modules/%{kverstr}/System.map
%exclude /lib/modules/%{kverstr}/config
%exclude /lib/modules/%{kverstr}/symvers.gz
%exclude /lib/modules/%{kverstr}/build
%exclude /lib/modules/%{kverstr}/source

%files devel
/usr/src/kernels/%{kverstr}
/lib/modules/%{kverstr}/build
/lib/modules/%{kverstr}/source

%changelog
* Thu Aug 20 2026 OpenGamingCollective CI
- Initial linux-unstable-ogc spec, generated by CI from linux-next.