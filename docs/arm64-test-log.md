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

## 2026-11-20T14:04:32-08:00 — adopt-krc @ c098d4e

- Host: BitWhiz
- Range: a6bdc1a..c098d4e, 2 commits
- Docs-only: no
- Configure: linux-gcc — ok, 1 s
- Build: linux-gcc-debug — ok, 20 s
- Test: linux-gcc-debug — 56/57 passed, 1 failed, 22 s
- Failed: kr16.unit — embedded runtime sources match checked-in resources
  (test_resources.cpp:33,36; kLibc +1029 bytes, kFileio +1465 bytes vs sources — CRLF in embedded arrays)
- Skipped: none
- Next: fix embedded_libc.cpp line endings

## 2026-11-20T23:08:01-08:00 — adopt-krc @ 5ec6eb9

- Host: BitWhiz
- Range: c098d4e..5ec6eb9, 3 commits
- Docs-only: no
- Configure: linux-gcc — ok, 1 s
- Build: linux-gcc-debug — ok, 9 s
- Test: linux-gcc-debug — 56/57 passed, 1 failed, 20 s
- Failed: kr16.unit — embedded runtime sources match checked-in resources
  (test_resources.cpp:33,36; kLibc +1048 bytes, kFileio +1465 bytes vs sources — CRLF in embedded arrays)
- Skipped: none
- Next: fix embedded_libc.cpp line endings
