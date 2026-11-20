# ARM64 test log

## 2026-11-20T13:54:00-08:00 — adopt-krc @ a6bdc1a

- Host: BitWhiz
- Range: first run — b9f7436..a6bdc1a, 2 commits
- Docs-only: no
- Configure: linux-gcc — ok, 1 s
- Build: linux-gcc-debug — ok, 2 s (no work)
- Test: linux-gcc-debug — 56/57 passed, 1 failed, 20 s
- Failed: kr16.unit — embedded runtime sources match checked-in resources
  (test_resources.cpp:33,36; kLibc +1029 bytes, kFileio +1465 bytes vs sources — CRLF in embedded arrays)
- Skipped: none
- Next: fix embedded_libc.cpp line endings
