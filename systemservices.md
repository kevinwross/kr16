# KR16 System Services (`SYS`) Specification

Status: **normative interface; implementation is incremental**

This document defines the C-facing interface to KR16 host services. The
instruction is now named `SYS` (previously `SYSCALL`). The VM implements the
sandboxed file, time, random, and filesystem-information services below; the
libc exposes a small stdio layer on top of them. See `docs/deferred.md` for the
remaining C library gaps.

## 1. Design decisions

### 1.1 Per-service structures, not a generic union

Each service gets its own argument and result types:

```c
struct sys_open_args {
    char *path;
    sys_u16 flags;
    sys_u16 mode;
};

struct sys_open_result {
    sys_fd fd;
};
```

This is preferred over a single `arg0..arg3` structure or an anonymous union
because:

- the compiler type-checks the fields;
- the service contract is self-documenting;
- no union punning is required;
- a service can grow without changing the calling convention.

Unions are supported by the KR16 C compiler, but they are not needed in the
public API. The only shared low-level object is the private 16-byte host-call
block used by the libc wrappers.

### 1.2 C calling convention

All public functions:

- return an `int` status (`SYS_OK` or a portable error code);
- take a pointer to a service-specific argument structure;
- take an optional pointer to a service-specific result structure;
- never expose native host handles, pointers, or OS APIs.

A result pointer may be `NULL` when the caller does not need the result. An
argument pointer may be `NULL` only for services whose arguments are entirely
reserved (currently `SYS_VERSION`, `SYS_TIME`, and `SYS_RANDOM`).

## 2. Reference C header

The public header is `include/kr16/systemservices.h`. The types below are
deliberately based on KR16 primitive sizes rather than `<stdint.h>`; on KR16
`int` and pointers are 16 bits. The header currently contains declarations and
ABI types; the `sys_*` wrappers will be supplied as each host service is
implemented.

```c
#ifndef KR16_SYSTEMSERVICES_H
#define KR16_SYSTEMSERVICES_H

/* KR16 primitive sizes: char=1, int=2, pointer=2. */
typedef unsigned char sys_u8;
typedef unsigned int  sys_u16;
typedef int           sys_s16;
typedef sys_s16       sys_fd;

/* Little-endian 32-bit pairs. 16-bit words are stored low word first. */
typedef struct sys_u32 {
    sys_u16 lo;
    sys_u16 hi;
} sys_u32;

typedef struct sys_i32 {
    sys_u16 lo;
    sys_u16 hi;
} sys_i32;

/* Service numbers. These values are stable. */
enum {
    SYS_VERSION = 0,
    SYS_OPEN    = 1,
    SYS_CLOSE   = 2,
    SYS_READ    = 3,
    SYS_WRITE   = 4,
    SYS_SEEK    = 5,
    SYS_STAT    = 6,
    SYS_UNLINK  = 7,
    SYS_RENAME  = 8,
    SYS_MKDIR   = 9,
    SYS_READDIR = 10,
    SYS_TIME    = 11,
    SYS_RANDOM  = 12,
    SYS_FSINFO  = 13
};

/* Status codes. */
enum {
    SYS_OK        = 0,
    SYS_ENOENT    = 1,
    SYS_EACCES    = 2,
    SYS_EINVAL    = 3,
    SYS_EBADF     = 4,
    SYS_EFAULT    = 5,
    SYS_ENOSPC    = 6,
    SYS_EEXIST    = 7,
    SYS_ENOSYS    = 8,
    SYS_EIO       = 9,
    SYS_EMFILE    = 10,
    SYS_ENAMETOOLONG = 11,
    SYS_ENOTDIR   = 12,
    SYS_EISDIR    = 13,
    SYS_ELOOP     = 14
};

/* SYS_OPEN flags. */
enum {
    SYS_O_RDONLY = 0x0000,
    SYS_O_WRONLY = 0x0001,
    SYS_O_RDWR   = 0x0002,
    SYS_O_CREAT  = 0x0100,
    SYS_O_TRUNC  = 0x0200,
    SYS_O_APPEND = 0x0400,
    SYS_O_EXCL   = 0x0800
};

/* SYS_SEEK whence values. */
enum {
    SYS_SEEK_SET = 0,
    SYS_SEEK_CUR = 1,
    SYS_SEEK_END = 2
};

/* SYS_STAT mode bits. Permission bits are reserved for a later service. */
enum {
    SYS_S_IFREG = 0x8000,
    SYS_S_IFDIR = 0x4000
};

/* SYS_READDIR entry types. */
enum {
    SYS_DT_UNKNOWN = 0,
    SYS_DT_REG     = 1,
    SYS_DT_DIR     = 2
};

struct sys_version_args {
    sys_u16 reserved;
};

struct sys_version_result {
    sys_u16 abi;
    sys_u16 capabilities;
    sys_u16 reserved;
};

struct sys_open_args {
    char *path;
    sys_u16 flags;
    sys_u16 mode;       /* reserved: must be 0 in ABI version 1 */
};

struct sys_open_result {
    sys_fd fd;
};

struct sys_close_args {
    sys_fd fd;
};

struct sys_read_args {
    sys_fd fd;
    void *buffer;
    sys_u16 count;
};

struct sys_read_result {
    sys_u16 bytes;
};

struct sys_write_args {
    sys_fd fd;
    void *buffer;
    sys_u16 count;
};

struct sys_write_result {
    sys_u16 bytes;
};

struct sys_seek_args {
    sys_fd fd;
    sys_i32 offset;     /* signed 32-bit value, two's complement */
    sys_u16 whence;
};

struct sys_seek_result {
    sys_u32 offset;
};

/* The stat buffer must be at least sizeof(struct sys_stat). */
struct sys_stat {
    sys_u16 mode;       /* SYS_S_IFREG / SYS_S_IFDIR plus reserved bits */
    sys_u16 reserved;
    sys_u32 size;       /* bytes */
    sys_u32 mtime;      /* Unix epoch seconds, 0 if unavailable */
};

struct sys_stat_args {
    char *path;
    struct sys_stat *buffer;
};

struct sys_unlink_args {
    char *path;
};

struct sys_rename_args {
    char *old_path;
    char *new_path;
};

struct sys_mkdir_args {
    char *path;
};

/* 64-byte entries; a 4096-byte buffer holds up to 64 entries. */
struct sys_dirent {
    sys_u16 type;       /* SYS_DT_* */
    sys_u16 name_length;/* excluding the terminating NUL */
    char name[60];
};

struct sys_readdir_args {
    sys_fd fd;
    struct sys_dirent *buffer;
    sys_u16 count;
};

struct sys_readdir_result {
    sys_u16 count;      /* entries written, 0 at end */
};

/* Three little-endian 32-bit values. */
struct sys_fsinfo {
    sys_u32 used;
    sys_u32 quota;
    sys_u32 free;
};

struct sys_fsinfo_args {
    struct sys_fsinfo *buffer;
};

struct sys_time_args {
    sys_u16 reserved;
};

struct sys_time_result {
    sys_u32 milliseconds; /* milliseconds since VM start */
};

struct sys_random_args {
    sys_u16 reserved;
};

struct sys_random_result {
    sys_u16 value;
};

int sys_version(const struct sys_version_args *args,
                struct sys_version_result *result);
int sys_open(const struct sys_open_args *args,
             struct sys_open_result *result);
int sys_close(const struct sys_close_args *args);
int sys_read(const struct sys_read_args *args,
             struct sys_read_result *result);
int sys_write(const struct sys_write_args *args,
              struct sys_write_result *result);
int sys_seek(const struct sys_seek_args *args,
             struct sys_seek_result *result);
int sys_stat(const struct sys_stat_args *args);
int sys_unlink(const struct sys_unlink_args *args);
int sys_rename(const struct sys_rename_args *args);
int sys_mkdir(const struct sys_mkdir_args *args);
int sys_readdir(const struct sys_readdir_args *args,
                struct sys_readdir_result *result);
int sys_time(const struct sys_time_args *args,
             struct sys_time_result *result);
int sys_random(const struct sys_random_args *args,
               struct sys_random_result *result);
int sys_fsinfo(const struct sys_fsinfo_args *args);

#endif /* KR16_SYSTEMSERVICES_H */
```

## 3. Calling convention and block mapping

`SYS #n` uses a 16-byte, 2-byte-aligned block in guest RAM. `R0` contains the
block address. The block is private to the libc wrapper and has this layout:

```c
struct sys_call_block {
    sys_u16 status;     /* word 0 */
    sys_u16 result[3];  /* words 1..3 */
    sys_u16 arg[4];     /* words 4..7 */
};
```

The VM writes `status` and `result`; it reads `arg`. The block pointer in `R0`
does not change. Flags are unchanged. The instruction charges 10 simulated
cycles.

The wrapper pattern is:

```c
int sys_open(const struct sys_open_args *args,
             struct sys_open_result *result) {
    struct sys_call_block block;
    int status;

    block.status = 0;
    block.result[0] = 0;
    block.result[1] = 0;
    block.result[2] = 0;
    block.arg[0] = (sys_u16)args->path;
    block.arg[1] = args->flags;
    block.arg[2] = args->mode;
    block.arg[3] = 0;

    status = __host_call(SYS_OPEN, &block);

    if (result != 0) {
        result->fd = (sys_fd)block.result[0];
    }
    return status;
}
```

`__host_call(service, block)` is the compiler intrinsic. It emits `SYS #n`
with the block pointer and returns the block's status word. Application code
should use the `sys_*` wrappers, not the intrinsic directly.

### 3.1 Argument word mapping

| Service | arg0 | arg1 | arg2 | arg3 |
|---|---|---|---|---|
| `SYS_VERSION` | 0 | 0 | 0 | 0 |
| `SYS_OPEN` | path | flags | mode | 0 |
| `SYS_CLOSE` | fd | 0 | 0 | 0 |
| `SYS_READ` | fd | buffer | count | 0 |
| `SYS_WRITE` | fd | buffer | count | 0 |
| `SYS_SEEK` | fd | offset.lo | offset.hi | whence |
| `SYS_STAT` | path | stat buffer | 0 | 0 |
| `SYS_UNLINK` | path | 0 | 0 | 0 |
| `SYS_RENAME` | old path | new path | 0 | 0 |
| `SYS_MKDIR` | path | 0 | 0 | 0 |
| `SYS_READDIR` | fd | dirent buffer | count | 0 |
| `SYS_TIME` | 0 | 0 | 0 | 0 |
| `SYS_RANDOM` | 0 | 0 | 0 | 0 |
| `SYS_FSINFO` | fsinfo buffer | 0 | 0 | 0 |

### 3.2 Result word mapping

| Service | result0 | result1 | result2 |
|---|---|---|---|
| `SYS_VERSION` | abi | capabilities | reserved |
| `SYS_OPEN` | fd | 0 | 0 |
| `SYS_READ` / `SYS_WRITE` | bytes | 0 | 0 |
| `SYS_SEEK` | offset.lo | offset.hi | 0 |
| `SYS_READDIR` | count | 0 | 0 |
| `SYS_TIME` | milliseconds.lo | milliseconds.hi | 0 |
| `SYS_RANDOM` | value | 0 | 0 |

`SYS_CLOSE`, `SYS_STAT`, `SYS_UNLINK`, `SYS_RENAME`, `SYS_MKDIR`, and
`SYS_FSINFO` return status only.

All guest pointers in `arg[0..3]` are 16-bit guest addresses. The host validates
each pointer against its declared buffer length. The block itself must be
2-byte aligned and must not cross `0xEFFF`. Invalid blocks or pointers return
`SYS_EFAULT`.

## 4. Service definitions

### 4.1 `SYS_VERSION` (`0`)

```c
struct sys_version_result {
    sys_u16 abi;
    sys_u16 capabilities;
    sys_u16 reserved;
};
```

Returns `abi = 1`. Capability bits:

| Bit | Meaning |
|---:|---|
| 0 | host filesystem |
| 1 | directories |
| 2 | time |
| 3 | random |
| 4 | console file descriptors |
| 5 | filesystem information/quota |

Currently bit 4 is set by the VM; the file, time, random, and fsinfo bits are
reported only when their services are implemented.

### 4.2 `SYS_OPEN` (`1`)

```c
struct sys_open_args {
    char *path;
    sys_u16 flags;
    sys_u16 mode;
};
struct sys_open_result {
    sys_fd fd;
};
```

- `path` is a NUL-terminated guest path, maximum 255 bytes including NUL.
- `mode` must be 0 in ABI version 1.
- File descriptors 0/1/2 are console; 3.. `SYS_MAX_FD-1` are files.
- On success, `fd` is 3 or greater.

### 4.3 `SYS_CLOSE` (`2`)

```c
struct sys_close_args {
    sys_fd fd;
};
```

Returns `SYS_EBADF` for an invalid or console descriptor.

### 4.4 `SYS_READ` (`3`)

```c
struct sys_read_args {
    sys_fd fd;
    void *buffer;
    sys_u16 count;
};
struct sys_read_result {
    sys_u16 bytes;
};
```

- `count` is at most `SYS_MAX_TRANSFER` (4096).
- On success, `bytes` is the number read.
- End of file is a successful read of zero bytes.

### 4.5 `SYS_WRITE` (`4`)

```c
struct sys_write_args {
    sys_fd fd;
    void *buffer;
    sys_u16 count;
};
struct sys_write_result {
    sys_u16 bytes;
};
```

Same limits as `SYS_READ`. A short write caused by quota exhaustion returns
`SYS_ENOSPC` and reports the number of bytes actually written.

### 4.6 `SYS_SEEK` (`5`)

```c
struct sys_seek_args {
    sys_fd fd;
    sys_i32 offset;
    sys_u16 whence;
};
struct sys_seek_result {
    sys_u32 offset;
};
```

`offset` is a two's-complement 32-bit value. `whence` is one of
`SYS_SEEK_SET`, `SYS_SEEK_CUR`, or `SYS_SEEK_END`. On success, `result.offset`
is the new absolute offset. Seeking before zero returns `SYS_EINVAL`.

### 4.7 `SYS_STAT` (`6`)

```c
struct sys_stat_args {
    char *path;
    struct sys_stat *buffer;
};
struct sys_stat {
    sys_u16 mode;
    sys_u16 reserved;
    sys_u32 size;
    sys_u32 mtime;
};
```

- `buffer` must point to at least `sizeof(struct sys_stat)` bytes.
- `mode` contains `SYS_S_IFREG` or `SYS_S_IFDIR`; other bits are reserved.
- `size` is the file size in bytes.
- `mtime` is Unix epoch seconds, or 0 when the host cannot provide it.

### 4.8 `SYS_UNLINK` (`7`)

```c
struct sys_unlink_args {
    char *path;
};
```

Removes a regular file. Directory removal is not part of ABI version 1; use of
a directory path returns `SYS_EISDIR`.

### 4.9 `SYS_RENAME` (`8`)

```c
struct sys_rename_args {
    char *old_path;
    char *new_path;
};
```

Both paths are guest paths in the same sandbox. Existing destination policy is
part of the host filesystem implementation; overwrite is not implied by the
ABI.

### 4.10 `SYS_MKDIR` (`9`)

```c
struct sys_mkdir_args {
    char *path;
};
```

Creates one directory inside the sandbox. Parent directories must exist.
Directory depth is limited to 16.

### 4.11 `SYS_READDIR` (`10`)

```c
struct sys_dirent {
    sys_u16 type;
    sys_u16 name_length;
    char name[60];
};
struct sys_readdir_args {
    sys_fd fd;
    struct sys_dirent *buffer;
    sys_u16 count;
};
struct sys_readdir_result {
    sys_u16 count;
};
```

- `count` is the number of 64-byte entries requested, up to 64.
- `name_length` excludes the terminating NUL; names are at most 59 bytes.
- `type` is `SYS_DT_REG`, `SYS_DT_DIR`, or `SYS_DT_UNKNOWN`.
- A successful call with `result.count == 0` means end of directory.
- Repeated calls advance the directory cursor.

### 4.12 `SYS_TIME` (`11`)

```c
struct sys_time_result {
    sys_u32 milliseconds;
};
```

Returns **milliseconds since VM start**. This is monotonic and deterministic
for tests; it is not a wall-clock service in ABI version 1.

### 4.13 `SYS_RANDOM` (`12`)

```c
struct sys_random_result {
    sys_u16 value;
};
```

Returns a 16-bit pseudorandom value. This is not a cryptographic random source.

### 4.14 `SYS_FSINFO` (`13`)

```c
struct sys_fsinfo {
    sys_u32 used;
    sys_u32 quota;
    sys_u32 free;
};
struct sys_fsinfo_args {
    struct sys_fsinfo *buffer;
};
```

The caller supplies a buffer of at least `sizeof(struct sys_fsinfo)`. Values
are in bytes. `free` is `quota - used`, clamped to zero.

## 5. Status codes

| Code | Name | Meaning |
|---:|---|---|
| 0 | `SYS_OK` | Success |
| 1 | `SYS_ENOENT` | No such file or directory |
| 2 | `SYS_EACCES` | Permission or sandbox denial |
| 3 | `SYS_EINVAL` | Invalid argument |
| 4 | `SYS_EBADF` | Bad file descriptor |
| 5 | `SYS_EFAULT` | Invalid guest block or pointer |
| 6 | `SYS_ENOSPC` | No space/quota exhausted |
| 7 | `SYS_EEXIST` | Already exists |
| 8 | `SYS_ENOSYS` | Service unavailable or host FS disabled |
| 9 | `SYS_EIO` | Host I/O error |
| 10 | `SYS_EMFILE` | Too many open files |
| 11 | `SYS_ENAMETOOLONG` | Path too long |
| 12 | `SYS_ENOTDIR` | Not a directory |
| 13 | `SYS_EISDIR` | Is a directory |
| 14 | `SYS_ELOOP` | Symlink loop or sandbox escape |

A failed call returns the status and leaves result fields unspecified unless
the service description says otherwise.

## 6. Host filesystem sandbox

Host file services are enabled by default with a lazy `./sandbox` root. They
honor `--no-host-fs` and the policy below:

- Default root: `./sandbox`, created lazily on first file operation.
- `--fs-root DIR`: use an existing directory; never auto-created.
- `--fs-readonly`: allow open-for-read, read, seek, stat, and readdir; deny
  create, write, truncate, unlink, rename, and mkdir.
- `--fs-quota SIZE`: default 5 MiB.
- `--no-host-fs`: all file services return `SYS_ENOSYS`.
- Guest paths are virtual: `/foo.txt` maps inside the sandbox.
- Reject: `..`, host absolute paths, drive letters, UNC paths, backslashes,
  control characters, symlinks/reparse points, reserved device names, alternate
  data streams, trailing dots/spaces, and names violating the portable filename
  policy.
- Limits: 16 open files, 4096-byte transfers, 255-byte paths, directory depth
  16, 1024 entries per directory.
- Quota is the logical total of regular files under the sandbox, including
  pre-existing files.

See `docs/deferred.md` for the current implementation status.

## 7. Examples

### 7.1 Query the ABI

```c
#include <kr16/systemservices.h>

int main(void) {
    struct sys_version_result version;

    if (sys_version(0, &version) != SYS_OK) {
        return 1;
    }
    if (version.abi != 1) {
        return 2;
    }
    return 0;
}
```

### 7.2 Open, write, close

```c
struct sys_open_args open_args;
struct sys_open_result open_result;
struct sys_write_args write_args;
struct sys_write_result write_result;
char message[] = "hello\n";

open_args.path = "/hello.txt";
open_args.flags = SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC;
open_args.mode = 0;

if (sys_open(&open_args, &open_result) != SYS_OK) {
    return 1;
}

write_args.fd = open_result.fd;
write_args.buffer = message;
write_args.count = sizeof(message) - 1;

if (sys_write(&write_args, &write_result) != SYS_OK) {
    sys_close(&(struct sys_close_args){open_result.fd});
    return 2;
}

sys_close(&(struct sys_close_args){open_result.fd});
```

Compound literals may not be supported by the current compiler; use a named
`struct sys_close_args` object in portable code.

### 7.3 Direct assembly

```asm
.text
.global _start
_start:
    MOV R0, #block
    SYS #SYS_VERSION
    HLT

.data
.align 2
block:
    .space 16
```

## 8. Implementation status

1. Public header and private `sys_call_block` helper: implemented.
2. `SYS_VERSION` wrapper: implemented.
3. `HostFs` sandbox and file services: implemented.
4. Time, random, and filesystem-information services: implemented.
5. C libc stdio wrappers (`fopen`, `fclose`, `fread`, `fwrite`, `fseek`,
   `ftell`, `fgetc`, `fputc`, `fgets`, `fputs`, `remove`, `rename`):
   implemented as an unbuffered subset.
6. Remaining: buffered stdio, `fprintf`/`fscanf`, directory/stat convenience
   functions in libc, standard streams, and the full POSIX wrapper surface.

Service numbers and struct field order are stable. Add new fields only as
previously reserved tail fields and bump `SYS_VERSION.abi` for incompatible
changes.

## 9. Open items

- Whether `SYS_TIME` should also expose wall-clock time in a future service.
- Whether `SYS_STAT` should gain an explicit `stat` size/version argument.
- Whether `SYS_READDIR` should gain a cookie-based iteration API.
- Final mode/permission semantics once a `chmod`-like service is considered.
