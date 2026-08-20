global-incdirs-y += include
srcs-y += remote_attestation_ta.c

# Performance measurement logging (pass CFG_REMOTE_ATTESTATION_PERF=y)
cflags-$(CFG_REMOTE_ATTESTATION_PERF) += -DCFG_REMOTE_ATTESTATION_PERF=1
