/*
 * KR16 C runtime: system-service wrappers and a small stdio layer.
 *
 * This file is compiled by kr16 cc and linked after libc.s. It deliberately
 * uses the fixed 16-byte host-call block described in systemservices.md.
 */

#include <kr16/systemservices.h>

typedef struct FILE FILE;

struct FILE {
    int fd;
    int readable;
    int writable;
    int eof;
    int error;
};

#define KR16_FILE_MAX 8
#define KR16_EOF (-1)

static struct FILE file_table[KR16_FILE_MAX];
static int file_used[KR16_FILE_MAX];

struct sys_call_block {
    sys_u16 status;
    sys_u16 result[3];
    sys_u16 arg[4];
};

int __host_call(int service, void *block);

static struct FILE *file_alloc(void) {
    int i;
    for (i = 0; i < KR16_FILE_MAX; ++i) {
        if (!file_used[i]) {
            file_used[i] = 1;
            file_table[i].eof = 0;
            file_table[i].error = 0;
            return &file_table[i];
        }
    }
    return 0;
}

static void file_release(struct FILE *stream) {
    int i;
    for (i = 0; i < KR16_FILE_MAX; ++i) {
        if (stream == &file_table[i]) {
            file_used[i] = 0;
            return;
        }
    }
}

static void block_clear(struct sys_call_block *block) {
    block->status = 0;
    block->result[0] = 0;
    block->result[1] = 0;
    block->result[2] = 0;
    block->arg[0] = 0;
    block->arg[1] = 0;
    block->arg[2] = 0;
    block->arg[3] = 0;
}

int sys_version(const struct sys_version_args *args,
                struct sys_version_result *result) {
    struct sys_call_block block;
    int status;
    (void)args;
    block_clear(&block);
    status = __host_call(0, &block);
    if (result != 0) {
        result->abi = block.result[0];
        result->capabilities = block.result[1];
        result->reserved = block.result[2];
    }
    return status;
}

int sys_open(const struct sys_open_args *args,
             struct sys_open_result *result) {
    struct sys_call_block block;
    int status;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->path;
    block.arg[1] = args->flags;
    block.arg[2] = args->mode;
    status = __host_call(1, &block);
    if (result != 0) {
        result->fd = (sys_fd)block.result[0];
    }
    return status;
}

int sys_close(const struct sys_close_args *args) {
    struct sys_call_block block;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->fd;
    return __host_call(2, &block);
}

int sys_read(const struct sys_read_args *args,
             struct sys_read_result *result) {
    struct sys_call_block block;
    int status;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->fd;
    block.arg[1] = (sys_u16)args->buffer;
    block.arg[2] = args->count;
    status = __host_call(3, &block);
    if (result != 0) {
        result->bytes = block.result[0];
    }
    return status;
}

int sys_write(const struct sys_write_args *args,
              struct sys_write_result *result) {
    struct sys_call_block block;
    int status;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->fd;
    block.arg[1] = (sys_u16)args->buffer;
    block.arg[2] = args->count;
    status = __host_call(4, &block);
    if (result != 0) {
        result->bytes = block.result[0];
    }
    return status;
}

int sys_seek(const struct sys_seek_args *args,
             struct sys_seek_result *result) {
    struct sys_call_block block;
    int status;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->fd;
    block.arg[1] = args->offset.lo;
    block.arg[2] = args->offset.hi;
    block.arg[3] = args->whence;
    status = __host_call(5, &block);
    if (result != 0) {
        result->offset.lo = block.result[0];
        result->offset.hi = block.result[1];
    }
    return status;
}

int sys_stat(const struct sys_stat_args *args) {
    struct sys_call_block block;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->path;
    block.arg[1] = (sys_u16)args->buffer;
    return __host_call(6, &block);
}

int sys_unlink(const struct sys_unlink_args *args) {
    struct sys_call_block block;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->path;
    return __host_call(7, &block);
}

int sys_rename(const struct sys_rename_args *args) {
    struct sys_call_block block;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->old_path;
    block.arg[1] = (sys_u16)args->new_path;
    return __host_call(8, &block);
}

int sys_mkdir(const struct sys_mkdir_args *args) {
    struct sys_call_block block;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->path;
    return __host_call(9, &block);
}

int sys_readdir(const struct sys_readdir_args *args,
                struct sys_readdir_result *result) {
    struct sys_call_block block;
    int status;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->fd;
    block.arg[1] = (sys_u16)args->buffer;
    block.arg[2] = args->count;
    status = __host_call(10, &block);
    if (result != 0) {
        result->count = block.result[0];
    }
    return status;
}

int sys_time(const struct sys_time_args *args,
             struct sys_time_result *result) {
    struct sys_call_block block;
    int status;
    (void)args;
    block_clear(&block);
    status = __host_call(11, &block);
    if (result != 0) {
        result->milliseconds.lo = block.result[0];
        result->milliseconds.hi = block.result[1];
    }
    return status;
}

int sys_random(const struct sys_random_args *args,
               struct sys_random_result *result) {
    struct sys_call_block block;
    int status;
    (void)args;
    block_clear(&block);
    status = __host_call(12, &block);
    if (result != 0) {
        result->value = block.result[0];
    }
    return status;
}

int sys_fsinfo(const struct sys_fsinfo_args *args) {
    struct sys_call_block block;
    if (args == 0) {
        return SYS_EINVAL;
    }
    block_clear(&block);
    block.arg[0] = (sys_u16)args->buffer;
    return __host_call(13, &block);
}

static int mode_flags(const char *mode, int *readable, int *writable) {
    int flags = SYS_O_RDONLY;
    int index = 0;
    if (mode == 0 || mode[0] == '\0') {
        return -1;
    }
    if (mode[0] == 'r') {
        flags = SYS_O_RDONLY;
        *readable = 1;
        *writable = 0;
    } else if (mode[0] == 'w') {
        flags = SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC;
        *readable = 0;
        *writable = 1;
    } else if (mode[0] == 'a') {
        flags = SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND;
        *readable = 0;
        *writable = 1;
    } else {
        return -1;
    }
    index = 1;
    while (mode[index] != '\0') {
        if (mode[index] == '+') {
            flags = (flags & ~3) | SYS_O_RDWR;
            *readable = 1;
            *writable = 1;
        } else if (mode[index] == 'x') {
            flags = flags | SYS_O_EXCL;
        } else if (mode[index] != 'b') {
            return -1;
        }
        ++index;
    }
    return flags;
}

FILE *fopen(const char *path, const char *mode) {
    struct sys_open_args args;
    struct sys_open_result result;
    struct FILE *stream;
    int readable;
    int writable;
    int flags;
    if (path == 0 || mode == 0) {
        return 0;
    }
    readable = 0;
    writable = 0;
    flags = mode_flags(mode, &readable, &writable);
    if (flags < 0) {
        return 0;
    }
    args.path = (char *)path;
    args.flags = (sys_u16)flags;
    args.mode = 0;
    if (sys_open(&args, &result) != SYS_OK) {
        return 0;
    }
    stream = file_alloc();
    if (stream == 0) {
        struct sys_close_args close_args;
        close_args.fd = result.fd;
        sys_close(&close_args);
        return 0;
    }
    stream->fd = result.fd;
    stream->readable = readable;
    stream->writable = writable;
    return stream;
}

int fclose(FILE *stream) {
    struct sys_close_args args;
    int status;
    if (stream == 0) {
        return KR16_EOF;
    }
    args.fd = stream->fd;
    status = sys_close(&args);
    file_release(stream);
    return status == SYS_OK ? 0 : KR16_EOF;
}

int fread(void *buffer, int size, int count, FILE *stream) {
    struct sys_read_args args;
    struct sys_read_result result;
    int total;
    if (stream == 0 || !stream->readable || size <= 0 || count <= 0) {
        return 0;
    }
    total = size * count;
    if (total <= 0 || total > 4096) {
        total = 4096;
    }
    args.fd = stream->fd;
    args.buffer = buffer;
    args.count = (sys_u16)total;
    if (sys_read(&args, &result) != SYS_OK) {
        stream->error = 1;
        return 0;
    }
    if (result.bytes < (sys_u16)total) {
        stream->eof = 1;
    }
    return result.bytes / size;
}

int fwrite(const void *buffer, int size, int count, FILE *stream) {
    struct sys_write_args args;
    struct sys_write_result result;
    int total;
    if (stream == 0 || !stream->writable || size <= 0 || count <= 0) {
        return 0;
    }
    total = size * count;
    if (total <= 0 || total > 4096) {
        total = 4096;
    }
    args.fd = stream->fd;
    args.buffer = (void *)buffer;
    args.count = (sys_u16)total;
    if (sys_write(&args, &result) != SYS_OK) {
        stream->error = 1;
        return 0;
    }
    return result.bytes / size;
}

int fgetc(FILE *stream) {
    unsigned char byte;
    if (stream == 0 || !stream->readable) {
        return KR16_EOF;
    }
    if (fread(&byte, 1, 1, stream) != 1) {
        return KR16_EOF;
    }
    return (int)byte;
}

int fputc(int value, FILE *stream) {
    unsigned char byte;
    if (stream == 0 || !stream->writable) {
        return KR16_EOF;
    }
    byte = (unsigned char)value;
    if (fwrite(&byte, 1, 1, stream) != 1) {
        return KR16_EOF;
    }
    return (int)byte;
}

char *fgets(char *buffer, int size, FILE *stream) {
    int index;
    int value;
    if (buffer == 0 || size <= 0 || stream == 0) {
        return 0;
    }
    index = 0;
    while (index + 1 < size) {
        value = fgetc(stream);
        if (value == KR16_EOF) {
            break;
        }
        buffer[index] = (char)value;
        ++index;
        if (value == '\n') {
            break;
        }
    }
    if (index == 0) {
        return 0;
    }
    buffer[index] = '\0';
    return buffer;
}

int fputs(const char *text, FILE *stream) {
    int length;
    if (text == 0) {
        return KR16_EOF;
    }
    length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    if (length == 0) {
        return 0;
    }
    return fwrite(text, 1, length, stream) == length ? 0 : KR16_EOF;
}

int fseek(FILE *stream, int offset, int whence) {
    struct sys_seek_args args;
    struct sys_seek_result result;
    if (stream == 0) {
        return KR16_EOF;
    }
    args.fd = stream->fd;
    args.offset.lo = (sys_u16)(offset & 0xFFFF);
    args.offset.hi = offset < 0 ? 0xFFFF : 0;
    args.whence = (sys_u16)whence;
    if (sys_seek(&args, &result) != SYS_OK) {
        stream->error = 1;
        return KR16_EOF;
    }
    stream->eof = 0;
    return 0;
}

int ftell(FILE *stream) {
    struct sys_seek_args args;
    struct sys_seek_result result;
    if (stream == 0) {
        return KR16_EOF;
    }
    args.fd = stream->fd;
    args.offset.lo = 0;
    args.offset.hi = 0;
    args.whence = SYS_SEEK_CUR;
    if (sys_seek(&args, &result) != SYS_OK) {
        return KR16_EOF;
    }
    if (result.offset.hi != 0 || result.offset.lo > 32767) {
        return KR16_EOF;
    }
    return (int)result.offset.lo;
}

void rewind(FILE *stream) {
    (void)fseek(stream, 0, SYS_SEEK_SET);
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int feof(FILE *stream) {
    return stream != 0 ? stream->eof : 0;
}

int ferror(FILE *stream) {
    return stream != 0 ? stream->error : 0;
}

void clearerr(FILE *stream) {
    if (stream != 0) {
        stream->eof = 0;
        stream->error = 0;
    }
}

int remove(const char *path) {
    struct sys_unlink_args args;
    if (path == 0) {
        return KR16_EOF;
    }
    args.path = (char *)path;
    return sys_unlink(&args) == SYS_OK ? 0 : KR16_EOF;
}

int rename(const char *old_path, const char *new_path) {
    struct sys_rename_args args;
    if (old_path == 0 || new_path == 0) {
        return KR16_EOF;
    }
    args.old_path = (char *)old_path;
    args.new_path = (char *)new_path;
    return sys_rename(&args) == SYS_OK ? 0 : KR16_EOF;
}
