srcs-$(CFG_REMOTE_ATTESTATION_PTA) += remote_attestation.c
srcs-$(CFG_REMOTE_ATTESTATION_PTA) += base64.c
srcs-$(CFG_REMOTE_ATTESTATION_PTA) += cbor.c
srcs-$(CFG_REMOTE_ATTESTATION_PTA) += hash.c
srcs-$(CFG_REMOTE_ATTESTATION_PTA) += sign.c
srcs-$(CFG_REMOTE_ATTESTATION_PERF) += perf.c
srcs-$(CFG_REMOTE_ATTESTATION_PTA) += qcbor/qcbor_encode.c
srcs-$(CFG_REMOTE_ATTESTATION_PTA) += qcbor/ieee754.c
srcs-$(CFG_REMOTE_ATTESTATION_PTA) += qcbor/UsefulBuf.c
srcs-$(CFG_NXP_CAAM) += ocotp.c

incdirs-$(CFG_NXP_CAAM_ECC_DRV) += ../../drivers/crypto/caam/include

cflags-$(CFG_REMOTE_ATTESTATION_PTA) += -Wno-declaration-after-statement
cflags-$(CFG_REMOTE_ATTESTATION_PTA) += -Wno-redundant-decls
