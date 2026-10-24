# Cortex-M -Os code size of nanosemver.c, a ratchet: `make size-check` fails if it grows,
# and `make ratchet-update` lowers these to what it now is. Raising one is a deliberate
# edit. A toolchain bump moves sizes on its own.
NSV_SIZE_M0_MAX := 1852
NSV_SIZE_M4_MAX := 1854
