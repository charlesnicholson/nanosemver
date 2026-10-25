# Cortex-M -Os size of nanosemver.c, code, data and strings, without and with NSV_DIAG,
# a ratchet: `make size-check` fails if it grows, and `make ratchet-update` lowers these to
# what it now is. Raising one is a deliberate edit. A toolchain bump moves sizes on its own.
NSV_SIZE_M0_MAX := 2319
NSV_SIZE_M4_MAX := 2311
NSV_SIZE_M0_DIAG_MAX := 4588
NSV_SIZE_M4_DIAG_MAX := 4584
