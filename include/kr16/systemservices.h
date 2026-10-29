#ifndef KR16_SYSTEMSERVICES_H
#define KR16_SYSTEMSERVICES_H

/*
 * KR16 host-service C interface.
 *
 * This header is the normative ABI description. The sys_* wrappers are
 * implemented by resources/fileio.c and linked by the KR16 C runtime.
 * Applications call the wrappers, not the __host_call intrinsic directly.
 *
 * KR16 primitive sizes: char = 1, int = 2, pointer = 2.
 */

typedef unsigned char sys_u8;
typedef unsigned int sys_u16;
typedef int sys_s16;
typedef sys_s16 sys_fd;

/* Little-endian 32-bit pairs. The low word is stored first. */
typedef struct sys_u32 {
    sys_u16 lo;
    sys_u16 hi;
} sys_u32;

typedef struct sys_i32 {
    sys_u16 lo;
    sys_u16 hi;
} sys_i32;

/* Stable service numbers. */
enum {
    SYS_VERSION = 0,
    SYS_OPEN = 1,
    SYS_CLOSE = 2,
    SYS_READ = 3,
    SYS_WRITE = 4,
    SYS_SEEK = 5,
    SYS_STAT = 6,
    SYS_UNLINK = 7,
    SYS_RENAME = 8,
    SYS_MKDIR = 9,
    SYS_READDIR = 10,
    SYS_TIME = 11,
    SYS_RANDOM = 12,
    SYS_FSINFO = 13
};

/* Portable status codes. */
enum {
    SYS_OK = 0,
    SYS_ENOENT = 1,
    SYS_EACCES = 2,
    SYS_EINVAL = 3,
    SYS_EBADF = 4,
    SYS_EFAULT = 5,
    SYS_ENOSPC = 6,
    SYS_EEXIST = 7,
    SYS_ENOSYS = 8,
    SYS_EIO = 9,
    SYS_EMFILE = 10,
    SYS_ENAMETOOLONG = 11,
    SYS_ENOTDIR = 12,
    SYS_EISDIR = 13,
    SYS_ELOOP = 14
};

/* SYS_OPEN flags. */
enum {
    SYS_O_RDONLY = 0x0000,
    SYS_O_WRONLY = 0x0001,
    SYS_O_RDWR = 0x0002,
    SYS_O_CREAT = 0x0100,
    SYS_O_TRUNC = 0x0200,
    SYS_O_APPEND = 0x0400,
    SYS_O_EXCL = 0x0800
};

/* SYS_SEEK whence values. */
enum {
    SYS_SEEK_SET = 0,
    SYS_SEEK_CUR = 1,
    SYS_SEEK_END = 2
};

/* SYS_STAT mode bits. Permission bits are reserved in ABI version 1. */
enum {
    SYS_S_IFREG = 0x8000,
    SYS_S_IFDIR = 0x4000
};

/* SYS_READDIR entry types. */
enum {
    SYS_DT_UNKNOWN = 0,
    SYS_DT_REG = 1,
    SYS_DT_DIR = 2
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
    sys_u16 mode;
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
    sys_i32 offset;
    sys_u16 whence;
};

struct sys_seek_result {
    sys_u32 offset;
};

struct sys_stat {
    sys_u16 mode;
    sys_u16 reserved;
    sys_u32 size;
    sys_u32 mtime;
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
    sys_u32 milliseconds;
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
