SUMMARY = "Veraison Attestation Application for i.MX with OP-TEE"
DESCRIPTION = "Remote attestation application using Veraison service with OP-TEE TA and host application"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "optee-os-tadevkit optee-client openssl python3-cryptography-native"

inherit cargo rust-target-config python3native

# Attester source path (override in local.conf if needed)
VERAISON_ATTESTATION_EXTERNAL_SRC ?= "/attester/remote_attestation"

RUST_APICLIENT_REV = "8e1268e5011563c7955027033cf081c1228d268f"
SRC_URI = "git://github.com/veraison/rust-apiclient.git;protocol=https;nobranch=1;rev=${RUST_APICLIENT_REV};destsuffix=rust-apiclient"
SRC_URI += "file://Cargo.lock"

include veraison-attestation-crates.inc

S = "${WORKDIR}/src"

# OP-TEE configuration
TA_DEV_KIT_DIR = "${STAGING_INCDIR}/optee/export-user_ta"
TEEC_EXPORT = "${STAGING_DIR_HOST}${prefix}"

# Rust configuration - rust-apiclient FFI library
CARGO_SRC_DIR = "${S}/host/rust-ffi/coserv-rs"
CARGO_MANIFEST_PATH = "${S}/host/rust-ffi/coserv-rs/Cargo.toml"
CARGO_PACKAGE = "veraison-apiclient-ffi"
CARGO_BUILD_FLAGS = "-v --frozen --target ${RUST_HOST_SYS} --release --manifest-path=${CARGO_MANIFEST_PATH} -p ${CARGO_PACKAGE}"

# python3-cryptography needs the legacy provider
export OPENSSL_MODULES = "${STAGING_LIBDIR_NATIVE}/ossl-modules"

python do_unpack:append() {
    import os
    import shutil

    src_base = d.getVar("VERAISON_ATTESTATION_EXTERNAL_SRC")
    if not os.path.isdir(src_base):
        bb.fatal(
            "Attester source not found at %s. "
            "Set VERAISON_ATTESTATION_EXTERNAL_SRC or mount /attester."
            % src_base
        )

    s = d.getVar("S")
    if os.path.exists(s):
        shutil.rmtree(s)
    shutil.copytree(src_base, s, symlinks=True)

    rust_src = os.path.join(d.getVar("WORKDIR"), "rust-apiclient")
    if not os.path.isdir(rust_src):
        rust_src = os.path.join(d.getVar("WORKDIR"), "sources-unpack", "rust-apiclient")
    if not os.path.isdir(rust_src):
        bb.fatal("rust-apiclient source not found at %s" % rust_src)

    dest = os.path.join(s, "host", "rust-ffi", "coserv-rs")
    if os.path.exists(dest):
        shutil.rmtree(dest)
    shutil.copytree(rust_src, dest, symlinks=True)

    lock_src = os.path.join(d.getVar("WORKDIR"), "Cargo.lock")
    if not os.path.isfile(lock_src):
        lock_src = os.path.join(d.getVar("WORKDIR"), "sources-unpack", "Cargo.lock")
    if os.path.isfile(lock_src):
        shutil.copyfile(lock_src, os.path.join(dest, "Cargo.lock"))
}

do_compile() {
    # Build Rust FFI library using Yocto's cargo infrastructure
    cd ${S}/host/rust-ffi/coserv-rs
    export OPENSSL_DIR="${STAGING_DIR_TARGET}/usr"
    export OPENSSL_LIB_DIR="${STAGING_DIR_TARGET}/usr/lib"
    export OPENSSL_INCLUDE_DIR="${STAGING_DIR_TARGET}/usr/include"
    export RUSTFLAGS="${RUSTFLAGS}"

    bbnote "Building Rust FFI with target ${RUST_HOST_SYS}"
    cargo build ${CARGO_BUILD_FLAGS}

    orig_cflags="${CFLAGS}"
    orig_ldflags="${LDFLAGS}"

    # Build TA - IMPORTANT: Clear Yocto's CFLAGS/LDFLAGS as OP-TEE TA build system
    # has its own flags and uses CROSS_COMPILE for the aarch64 toolchain
    cd ${S}/ta
    unset CFLAGS
    unset CPPFLAGS
    unset CXXFLAGS
    unset LDFLAGS

    # Clean any stale build artifacts that may have incorrect paths from QEMU builds
    rm -f .*.d .*.o.cmd *.o *.lds ta.lds dyn_list *.map *.dmp *.ta *.elf 2>/dev/null || true

    # Use oe_runmake with explicit TA build parameters
    oe_runmake V=1 \
        TA_DEV_KIT_DIR=${TA_DEV_KIT_DIR} \
        CROSS_COMPILE=${HOST_PREFIX} \
        LIBGCC_LOCATE_CFLAGS="--sysroot=${STAGING_DIR_HOST}"

    # Build host application
    cd ${S}/host

    # Clean any stale build artifacts from QEMU builds (x86_64 objects)
    rm -f *.o optee_remote_attestation optee_example_veraison_attestation 2>/dev/null || true

    export TEEC_EXPORT="${STAGING_DIR_HOST}/usr"
    export CROSS_COMPILE="${TARGET_PREFIX}"
    export RUST_APICLIENT_DIR="${S}/host/rust-ffi/coserv-rs"
    export RUST_APICLIENT_TARGET="${RUST_HOST_SYS}"
    export RUST_APICLIENT_LIB="${B}/target/${RUST_HOST_SYS}/release/libveraison_apiclient_ffi.a"
    # Restore CFLAGS for host app build
    export CFLAGS="${orig_cflags} --sysroot=${STAGING_DIR_HOST}"
    export LDFLAGS="${orig_ldflags} --sysroot=${STAGING_DIR_HOST}"
    oe_runmake
}

do_install() {
    # Install TA
    install -d ${D}${nonarch_base_libdir}/optee_armtz
    install -m 0444 ${S}/ta/*.ta ${D}${nonarch_base_libdir}/optee_armtz/

    # Install host application
    install -d ${D}${bindir}
    install -m 0755 ${S}/host/optee_remote_attestation ${D}${bindir}/
}

FILES:${PN} = "${bindir}/optee_remote_attestation"
FILES:${PN} += "${nonarch_base_libdir}/optee_armtz/*.ta"

RDEPENDS:${PN} = "optee-client"

INSANE_SKIP:${PN}-dbg += "buildpaths"
