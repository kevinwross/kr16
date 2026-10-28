# Platform Layer

All host-specific APIs belong here. The rest of the codebase must remain
portable and must not include Windows/POSIX headers directly.

The first platform services are console/keyboard I/O and the DAP TCP listener
in later milestones.
