SUMMARY = "Quote the IMA PCR with the fTPM and collect the measurement list"
DESCRIPTION = "Board-side script of the IMA attestation check: creates the \
EK/AK in the fTPM, quotes PCR 10 with a nonce and packs the quote together \
with the IMA measurement list for verification on another machine \
(attester/ima-quote/ in the repository)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ima-quote"

S = "${UNPACKDIR}"

RDEPENDS:${PN} = "tpm2-tools libtss2-tcti-device"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${UNPACKDIR}/ima-quote ${D}${bindir}/ima-quote
}
