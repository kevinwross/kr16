# ARM64 test log

## 2026-11-20T13:59:28-08:00 — main @ d3404fa

- Host: BitWhiz
- Range: first run — tip d3404fa, no prior log
- Docs-only: no
- Configure: linux-gcc — ok, 2 s
- Build: linux-gcc-debug — ok, 21 s
- Test: linux-gcc-debug — 56/57 passed, 1 failed, 21 s
- Failed: kr16.unit — embedded runtime sources match checked-in resources
  (test_resources.cpp:33,36,39; kLibc +1029, kFileio +1464, kSystemServices +242 bytes vs sources — CRLF in embedded arrays)
- Skipped: none
- Next: fix embedded_libc.cpp line endings
