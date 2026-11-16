/*
 * KR16 C runtime: system-service wrappers and a small stdio layer.
 *
 * This file is compiled by kr16 cc and linked after libc.s. It deliberately
 * uses the fixed 16-byte host-call block described in systemservices.md.
 */

#include <kr16/systemservices.h>
#include <stdarg.h>

typedef struct FILE FILE;

#define KR16_BUFFER_SIZE 64

struct FILE {
    int fd;
    int readable;
    int writable;
    int eof;
    int error;
    int mode;
    int buffered;
    int position;
    int length;
    int pushback;
    char temporary_path[16];
    unsigned char buffer[KR16_BUFFER_SIZE];
};

#define KR16_FILE_MAX 8
#define KR16_EOF (-1)
#define KR16_UNBUFFERED 0
#define KR16_FULL_BUFFER 1
#define KR16_LINE_BUFFER 2
#define KR16_STDIN ((FILE *)0)
#define KR16_STDOUT ((FILE *)1)
#define KR16_STDERR ((FILE *)2)

static struct FILE file_table[KR16_FILE_MAX];
static int file_used[KR16_FILE_MAX];

struct sys_call_block {
    sys_u16 status;
    sys_u16 result[3];
    sys_u16 arg[4];
};

int __host_call(int service, void *block);
int putchar(int value);
int getchar(void);

static struct FILE *file_alloc(void) {
    int i;
    for (i = 0; i < KR16_FILE_MAX; ++i) {
        if (!file_used[i]) {
            file_used[i] = 1;
            file_table[i].eof = 0;
            file_table[i].error = 0;
            file_table[i].mode = KR16_FULL_BUFFER;
            file_table[i].buffered = 0;
            file_table[i].position = 0;
            file_table[i].length = 0;
            file_table[i].pushback = -1;
            file_table[i].temporary_path[0] = 0;
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

int errno = 0;

int *__errno_location(void) {
    return &errno;
}

static int errno_from_status(int status) {
    switch (status) {
        case SYS_ENOENT: return 2;
        case SYS_EIO: return 5;
        case SYS_EBADF: return 9;
        case SYS_EACCES: return 13;
        case SYS_EFAULT: return 14;
        case SYS_EEXIST: return 17;
        case SYS_ENOTDIR: return 20;
        case SYS_EISDIR: return 21;
        case SYS_EINVAL: return 22;
        case SYS_EMFILE: return 24;
        case SYS_ENOSPC: return 28;
        case SYS_ENAMETOOLONG: return 36;
        case SYS_ENOSYS: return 38;
        default: return 5;
    }
}

static void set_errno_status(int status) {
    if (status != SYS_OK) {
        errno = errno_from_status(status);
    }
}

FILE *fopen(const char *path, const char *mode) {
    struct sys_open_args args;
    struct sys_open_result result;
    struct FILE *stream;
    int readable;
    int writable;
    int flags;
    if (path == 0 || mode == 0) {
        errno = 22;
        return 0;
    }
    readable = 0;
    writable = 0;
    flags = mode_flags(mode, &readable, &writable);
    if (flags < 0) {
        errno = 22;
        return 0;
    }
    args.path = (char *)path;
    args.flags = (sys_u16)flags;
    args.mode = 0;
    {
        int open_status = sys_open(&args, &result);
        if (open_status != SYS_OK) {
            set_errno_status(open_status);
            return 0;
        }
    }
    stream = file_alloc();
    if (stream == 0) {
        struct sys_close_args close_args;
        close_args.fd = result.fd;
        sys_close(&close_args);
        errno = 24;
        return 0;
    }
    stream->fd = result.fd;
    stream->readable = readable;
    stream->writable = writable;
    errno = 0;
    return stream;
}

static int flush_stream(FILE *stream) {
    struct sys_write_args args;
    struct sys_write_result result;
    if (stream == 0 || !stream->writable) {
        return 0;
    }
    if (stream == KR16_STDOUT || stream == KR16_STDERR) {
        return 0;
    }
    if (stream->length <= 0) {
        return 0;
    }
    args.fd = stream->fd;
    args.buffer = stream->buffer;
    args.count = (sys_u16)stream->length;
    if (sys_write(&args, &result) != SYS_OK ||
        result.bytes != (sys_u16)stream->length) {
        stream->error = 1;
        return KR16_EOF;
    }
    stream->length = 0;
    return 0;
}

static int refill_stream(FILE *stream) {
    struct sys_read_args args;
    struct sys_read_result result;
    if (stream == 0 || !stream->readable) {
        return 0;
    }
    args.fd = stream->fd;
    args.buffer = stream->buffer;
    args.count = KR16_BUFFER_SIZE;
    if (sys_read(&args, &result) != SYS_OK) {
        stream->error = 1;
        return 0;
    }
    stream->position = 0;
    stream->length = result.bytes;
    if (result.bytes == 0) {
        stream->eof = 1;
    }
    return result.bytes;
}

int fclose(FILE *stream) {
    struct sys_close_args args;
    int status;
    if (stream == 0) {
        errno = 9;
        return KR16_EOF;
    }
    if (stream == KR16_STDIN || stream == KR16_STDOUT ||
        stream == KR16_STDERR) {
        return 0;
    }
    if (flush_stream(stream) != 0) {
        errno = 5;
        return KR16_EOF;
    }
    args.fd = stream->fd;
    status = sys_close(&args);
    set_errno_status(status);
    if (stream->temporary_path[0] != 0) {
        struct sys_unlink_args unlink_args;
        unlink_args.path = stream->temporary_path;
        sys_unlink(&unlink_args);
    }
    file_release(stream);
    return status == SYS_OK ? 0 : KR16_EOF;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    struct sys_open_args open_args;
    struct sys_open_result result;
    struct sys_close_args close_args;
    int readable;
    int writable;
    int flags;
    int status;
    if (stream == 0 || path == 0 || mode == 0 ||
        stream == KR16_STDIN || stream == KR16_STDOUT ||
        stream == KR16_STDERR) {
        errno = 22;
        return 0;
    }
    if (flush_stream(stream) != 0) {
        errno = 5;
        return 0;
    }
    readable = 0;
    writable = 0;
    flags = mode_flags(mode, &readable, &writable);
    if (flags < 0) {
        errno = 22;
        return 0;
    }
    open_args.path = (char *)path;
    open_args.flags = (sys_u16)flags;
    open_args.mode = 0;
    status = sys_open(&open_args, &result);
    if (status != SYS_OK) {
        set_errno_status(status);
        close_args.fd = stream->fd;
        sys_close(&close_args);
        file_release(stream);
        return 0;
    }
    close_args.fd = stream->fd;
    sys_close(&close_args);
    stream->fd = result.fd;
    stream->readable = readable;
    stream->writable = writable;
    stream->eof = 0;
    stream->error = 0;
    stream->mode = KR16_FULL_BUFFER;
    stream->buffered = 0;
    stream->position = 0;
    stream->length = 0;
    stream->pushback = -1;
    stream->temporary_path[0] = 0;
    errno = 0;
    return stream;
}

FILE *tmpfile(void) {
    static int temporary_counter = 0;
    int attempt;
    for (attempt = 0; attempt < 100; ++attempt) {
        char path[16];
        int number = temporary_counter;
        int index;
        FILE *stream;
        temporary_counter = (temporary_counter + 1) % 100;
        path[0] = 'k';
        path[1] = 'r';
        path[2] = '1';
        path[3] = '6';
        path[4] = '_';
        path[5] = 't';
        path[6] = 'm';
        path[7] = 'p';
        path[8] = '_';
        path[9] = (char)(48 + number / 10);
        path[10] = (char)(48 + number % 10);
        path[11] = 0;
        stream = fopen(path, "w+x");
        if (stream != 0) {
            for (index = 0; index < 16; ++index) {
                stream->temporary_path[index] = path[index];
            }
            return stream;
        }
        if (errno != 17) {
            return 0;
        }
    }
    errno = 24;
    return 0;
}

int fread(void *buffer, int size, int count, FILE *stream) {
    unsigned char *destination;
    int total;
    int written;
    int value;
    if (stream == 0 || size <= 0 || count <= 0) {
        return 0;
    }
    destination = (unsigned char *)buffer;
    total = size * count;
    if (total <= 0 || total > 4096) {
        total = 4096;
    }
    if (stream == KR16_STDIN) {
        written = 0;
        while (written < total) {
            value = fgetc(KR16_STDIN);
            if (value == KR16_EOF) {
                break;
            }
            destination[written] = (unsigned char)value;
            ++written;
        }
        return written / size;
    }
    if (!stream->readable) {
        return 0;
    }
    written = 0;
    while (written < total) {
        if (stream->position >= stream->length) {
            if (refill_stream(stream) == 0) {
                break;
            }
        }
        destination[written] = stream->buffer[stream->position];
        ++stream->position;
        ++written;
    }
    return written / size;
}

int fwrite(const void *buffer, int size, int count, FILE *stream) {
    struct sys_write_args args;
    struct sys_write_result result;
    const unsigned char *source;
    int total;
    int written;
    int index;
    if (stream == 0 || size <= 0 || count <= 0) {
        return 0;
    }
    source = (const unsigned char *)buffer;
    total = size * count;
    if (total <= 0 || total > 4096) {
        total = 4096;
    }
    if (stream == KR16_STDOUT || stream == KR16_STDERR) {
        for (index = 0; index < total; ++index) {
            putchar(source[index]);
        }
        return total / size;
    }
    if (!stream->writable) {
        return 0;
    }
    if (stream->mode == KR16_UNBUFFERED) {
        args.fd = stream->fd;
        args.buffer = (void *)buffer;
        args.count = (sys_u16)total;
        if (sys_write(&args, &result) != SYS_OK) {
            stream->error = 1;
            return 0;
        }
        return result.bytes / size;
    }
    written = 0;
    while (written < total) {
        if (stream->length >= KR16_BUFFER_SIZE && flush_stream(stream) != 0) {
            break;
        }
        stream->buffer[stream->length] = source[written];
        ++stream->length;
        ++written;
        if (stream->mode == KR16_LINE_BUFFER &&
            source[written - 1] == '\n') {
            if (flush_stream(stream) != 0) {
                break;
            }
        }
    }
    return written / size;
}

int fgetc(FILE *stream) {
    int value;
    if (stream == 0) {
        return KR16_EOF;
    }
    if (stream == KR16_STDIN) {
        return getchar();
    }
    if (!stream->readable) {
        return KR16_EOF;
    }
    if (stream->pushback >= 0) {
        value = stream->pushback;
        stream->pushback = -1;
        return value;
    }
    if (stream->position >= stream->length) {
        if (refill_stream(stream) == 0) {
            return KR16_EOF;
        }
    }
    value = stream->buffer[stream->position];
    ++stream->position;
    return value;
}

int fputc(int value, FILE *stream) {
    unsigned char byte;
    if (stream == KR16_STDOUT || stream == KR16_STDERR) {
        return putchar(value);
    }
    if (stream == 0 || !stream->writable) {
        return KR16_EOF;
    }
    byte = (unsigned char)value;
    if (fwrite(&byte, 1, 1, stream) != 1) {
        return KR16_EOF;
    }
    return (int)byte;
}

int ungetc(int value, FILE *stream) {
    if (stream == 0 || value == KR16_EOF || stream->pushback >= 0) {
        return KR16_EOF;
    }
    stream->pushback = value & 0xFF;
    stream->eof = 0;
    return value & 0xFF;
}

int setvbuf(FILE *stream, char *buffer, int mode, int size) {
    (void)buffer;
    (void)size;
    if (stream == 0 || stream == KR16_STDIN || stream == KR16_STDOUT ||
        stream == KR16_STDERR) {
        return 0;
    }
    if (mode == 0) {
        stream->mode = KR16_UNBUFFERED;
    } else if (mode == 2) {
        stream->mode = KR16_LINE_BUFFER;
    } else {
        stream->mode = KR16_FULL_BUFFER;
    }
    return 0;
}

static void print_char(FILE *stream, int value, int *count) {
    if (fputc(value, stream) != KR16_EOF) {
        ++(*count);
    }
}

static void print_padded(FILE *stream, const char *text, int length, int width,
                         int left, int *count) {
    int i;
    if (!left) {
        for (i = length; i < width; ++i) {
            print_char(stream, ' ', count);
        }
    }
    for (i = 0; i < length; ++i) {
        print_char(stream, text[i], count);
    }
    if (left) {
        for (i = length; i < width; ++i) {
            print_char(stream, ' ', count);
        }
    }
}

static void print_number(FILE *stream, int value, int base, int upper,
                         int width, int pad, int prefix, int *count) {
    char digits[8];
    int length;
    int negative;
    int total;
    int i;
    char temp;
    negative = value < 0 && base == 10;
    if (negative) {
        value = -value;
    }
    length = 0;
    if (value < 0 && (base == 8 || base == 16)) {
        int bits = base == 16 ? 4 : 3;
        int mask = (1 << bits) - 1;
        int maximum = 16 / bits;
        while (length < maximum) {
            int digit = value & mask;
            digits[length] = (char)(digit < 10 ? '0' + digit
                                               : (upper ? 'A' : 'a') + digit - 10);
            ++length;
            value = (value >> bits) & ((1 << (16 - bits)) - 1);
        }
    } else {
        do {
            int digit = value % base;
            digits[length] = (char)(digit < 10 ? '0' + digit
                                               : (upper ? 'A' : 'a') + digit - 10);
            ++length;
            value /= base;
        } while (value != 0);
    }
    if (negative) {
        digits[length] = '-';
        ++length;
    }
    for (i = 0; i < length / 2; ++i) {
        temp = digits[i];
        digits[i] = digits[length - 1 - i];
        digits[length - 1 - i] = temp;
    }
    total = length + (prefix ? 2 : 0);
    for (i = total; i < width; ++i) {
        print_char(stream, pad, count);
    }
    if (prefix) {
        print_char(stream, '0', count);
        print_char(stream, 'x', count);
    }
    for (i = 0; i < length; ++i) {
        print_char(stream, digits[i], count);
    }
}
static void long_divide_small(int low, int high, int base,
                              int *out_low, int *out_high, int *remainder) {
    int quotient = 0;
    int rem = 0;
    int bit;
    for (bit = 15; bit >= 0; --bit) {
        rem = rem * 2 + ((high >> bit) & 1);
        if (rem >= base) {
            rem -= base;
            quotient |= (1 << bit);
        }
    }
    *out_high = quotient;
    quotient = 0;
    for (bit = 15; bit >= 0; --bit) {
        rem = rem * 2 + ((low >> bit) & 1);
        if (rem >= base) {
            rem -= base;
            quotient |= (1 << bit);
        }
    }
    *out_low = quotient;
    *remainder = rem;
}

static void print_long_words(FILE *stream, int low, int high, int base,
                             int upper, int width, int pad, int *count) {
    char digits[16];
    int length = 0;
    int negative = 0;
    int total;
    int index;
    char temp;
    if (base == 10 && (high & 32768) != 0) {
        int old_low = low;
        negative = 1;
        low = 0 - low;
        high = ~high;
        if (old_low == 0) {
            ++high;
        }
    }
    for (;;) {
        int remainder;
        long_divide_small(low, high, base, &low, &high, &remainder);
        digits[length] = (char)(remainder < 10 ? 48 + remainder
                                              : (upper ? 55 : 87) + remainder);
        ++length;
        if (low == 0 && high == 0) {
            break;
        }
    }
    if (negative) {
        digits[length] = 45;
        ++length;
    }
    for (index = 0; index < length / 2; ++index) {
        temp = digits[index];
        digits[index] = digits[length - 1 - index];
        digits[length - 1 - index] = temp;
    }
    total = length;
    for (index = total; index < width; ++index) {
        print_char(stream, pad, count);
    }
    for (index = 0; index < length; ++index) {
        print_char(stream, digits[index], count);
    }
}

static int format_stream(FILE *stream, const char *format, va_list args) {
    int count;
    int width;
    int pad;
    int left;
    int value;
    int long_flag;
    char *text;
    char byte[2];
    const char *cursor;
    if (stream == 0 || format == 0) {
        return KR16_EOF;
    }
    count = 0;
    cursor = format;
    while (*cursor != '\0') {
        if (*cursor != '%') {
            print_char(stream, *cursor, &count);
            ++cursor;
            continue;
        }
        ++cursor;
        pad = ' ';
        left = 0;
        width = 0;
        if (*cursor == '-') {
            left = 1;
            ++cursor;
        }
        if (*cursor == '0') {
            pad = '0';
            ++cursor;
        }
        while (*cursor >= '0' && *cursor <= '9') {
            width = width * 10 + (*cursor - '0');
            ++cursor;
        }
        long_flag = 0;
        if (*cursor == 'l') {
            long_flag = 1;
            ++cursor;
            if (*cursor == 'l') {
                ++cursor;
            }
        } else if (*cursor == 'h') {
            ++cursor;
        }
        if (long_flag && (*cursor == 'u' || *cursor == 'd' ||
                          *cursor == 'i' || *cursor == 'x' ||
                          *cursor == 'X' || *cursor == 'o')) {
            int low = va_arg(args, int);
            int high = va_arg(args, int);
            int base;
            int upper;
            if (*cursor == 'x' || *cursor == 'X') {
                base = 16;
            } else if (*cursor == 'o') {
                base = 8;
            } else {
                base = 10;
            }
            upper = *cursor == 'X';
            print_long_words(stream, low, high, base, upper, width, pad,
                             &count);
        } else if (*cursor == 'u') {
            value = va_arg(args, int);
            print_number(stream, value, 10, 0, width, pad, 0, &count);
        } else if (*cursor == 'd' || *cursor == 'i') {
            value = va_arg(args, int);
            print_number(stream, value, 10, 0, width, pad, 0, &count);
        } else if (*cursor == 'x' || *cursor == 'X') {
            value = va_arg(args, int);
            print_number(stream, value, 16, *cursor == 'X', width, pad, 0,
                         &count);
        } else if (*cursor == 'o') {
            value = va_arg(args, int);
            print_number(stream, value, 8, 0, width, pad, 0, &count);
        } else if (*cursor == 'p') {
            value = va_arg(args, int);
            print_number(stream, value, 16, 0, width, pad, 1, &count);
        } else if (*cursor == 'c') {
            value = va_arg(args, int);
            print_char(stream, value, &count);
        } else if (*cursor == 's') {
            text = va_arg(args, char *);
            if (text == 0) {
                text = (char *)"(null)";
            }
            value = 0;
            while (text[value] != '\0') {
                ++value;
            }
            print_padded(stream, text, value, width, left, &count);
        } else if (*cursor == '%') {
            print_char(stream, '%', &count);
        } else if (*cursor == '\0') {
            break;
        } else {
            print_char(stream, '%', &count);
            print_char(stream, *cursor, &count);
        }
        if (*cursor != '\0') {
            ++cursor;
        }
    }
    return count;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    int result;
    va_start(args, format);
    result = format_stream(stream, format, args);
    va_end(args);
    return result;
}

int printf(const char *format, ...) {
    va_list args;
    int result;
    va_start(args, format);
    result = format_stream(KR16_STDOUT, format, args);
    va_end(args);
    return result;
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
    if (stream == KR16_STDIN || stream == KR16_STDOUT ||
        stream == KR16_STDERR) {
        return stream == KR16_STDOUT ? 0 : KR16_EOF;
    }
    if (flush_stream(stream) != 0) {
        return KR16_EOF;
    }
    stream->position = 0;
    stream->length = 0;
    stream->pushback = -1;
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
    int offset;
    if (stream == 0) {
        return KR16_EOF;
    }
    if (stream == KR16_STDIN || stream == KR16_STDOUT ||
        stream == KR16_STDERR) {
        return 0;
    }
    if (flush_stream(stream) != 0) {
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
    offset = (int)result.offset.lo;
    offset -= stream->length - stream->position;
    return offset;
}

void rewind(FILE *stream) {
    (void)fseek(stream, 0, SYS_SEEK_SET);
}

int fflush(FILE *stream) {
    if (stream == 0) {
        int i;
        for (i = 0; i < KR16_FILE_MAX; ++i) {
            if (file_used[i] && flush_stream(&file_table[i]) != 0) {
                return KR16_EOF;
            }
        }
        return 0;
    }
    if (stream == KR16_STDIN || stream == KR16_STDOUT ||
        stream == KR16_STDERR) {
        return 0;
    }
    return flush_stream(stream);
}

int feof(FILE *stream) {
    if (stream == 0 || stream == KR16_STDIN || stream == KR16_STDOUT ||
        stream == KR16_STDERR) {
        return 0;
    }
    return stream->eof;
}

int ferror(FILE *stream) {
    if (stream == 0 || stream == KR16_STDIN || stream == KR16_STDOUT ||
        stream == KR16_STDERR) {
        return 0;
    }
    return stream->error;
}

void clearerr(FILE *stream) {
    if (stream != 0 && stream != KR16_STDIN && stream != KR16_STDOUT &&
        stream != KR16_STDERR) {
        stream->eof = 0;
        stream->error = 0;
    }
}

char *strerror(int error) {
    switch (error) {
        case 0: return "success";
        case 2: return "no such file or directory";
        case 5: return "input/output error";
        case 9: return "bad file descriptor";
        case 13: return "permission denied";
        case 14: return "bad address";
        case 17: return "file exists";
        case 20: return "not a directory";
        case 21: return "is a directory";
        case 22: return "invalid argument";
        case 24: return "too many open files";
        case 28: return "no space left on device";
        case 36: return "file name too long";
        case 38: return "function not implemented";
        default: return "unknown error";
    }
}

static int scan_is_space(int value) {
    return value == 32 || (value >= 9 && value <= 13);
}

static int scan_digit_value(int value, int base) {
    if (value >= 48 && value <= 57) {
        value -= 48;
    } else if (value >= 65 && value <= 70) {
        value -= 55;
    } else if (value >= 97 && value <= 102) {
        value -= 87;
    } else {
        return -1;
    }
    return value < base ? value : -1;
}

int fscanf(FILE *stream, const char *format, ...) {
    va_list args;
    int assigned;
    int value;
    int character;
    int index;
    int width;
    int suppress;
    int negative;
    int long_flag;
    if (stream == 0 || format == 0) {
        errno = 22;
        return KR16_EOF;
    }
    va_start(args, format);
    assigned = 0;
    while (*format != 0) {
        if (scan_is_space(*format)) {
            while (scan_is_space(*format)) {
                ++format;
            }
            for (;;) {
                character = fgetc(stream);
                if (character == KR16_EOF) {
                    va_end(args);
                    return assigned == 0 ? KR16_EOF : assigned;
                }
                if (!scan_is_space(character)) {
                    ungetc(character, stream);
                    break;
                }
            }
            continue;
        }
        if (*format != 37) {
            character = fgetc(stream);
            if (character == KR16_EOF) {
                va_end(args);
                return assigned == 0 ? KR16_EOF : assigned;
            }
            if (character != *format) {
                ungetc(character, stream);
                break;
            }
            ++format;
            continue;
        }
        ++format;
        suppress = 0;
        if (*format == 42) {
            suppress = 1;
            ++format;
        }
        width = 0;
        while (*format >= 48 && *format <= 57) {
            width = width * 10 + (*format - 48);
            ++format;
        }
        long_flag = 0;
        if (*format == 'l') {
            long_flag = 1;
            ++format;
            if (*format == 'l') {
                ++format;
            }
        }
        if (long_flag &&
            (*format == 100 || *format == 117 || *format == 120)) {
            int base = *format == 120 ? 16 : 10;
            int count = 0;
            long total = 0;
            int digit;
            negative = 0;
            for (;;) {
                character = fgetc(stream);
                if (character == KR16_EOF) {
                    break;
                }
                if (!scan_is_space(character)) {
                    ungetc(character, stream);
                    break;
                }
            }
            character = fgetc(stream);
            if (character == 43 || character == 45) {
                negative = character == 45;
                character = fgetc(stream);
            }
            if (character == KR16_EOF) {
                va_end(args);
                return assigned == 0 ? KR16_EOF : assigned;
            }
            for (;;) {
                digit = scan_digit_value(character, base);
                if (digit < 0) {
                    break;
                }
                total = total * base + digit;
                ++count;
                if (width > 0 && count >= width) {
                    character = KR16_EOF;
                    break;
                }
                character = fgetc(stream);
            }
            if (character != KR16_EOF) {
                ungetc(character, stream);
            }
            if (count == 0) {
                break;
            }
            if (negative) {
                total = -total;
            }
            if (!suppress) {
                int *target = va_arg(args, int *);
                *((long *)target) = total;
                ++assigned;
            }
            ++format;
            continue;
        }
        if (*format == 100 || *format == 117 || *format == 120) {
            int base = *format == 120 ? 16 : 10;
            int count = 0;
            int total = 0;
            int digit;
            negative = 0;
            for (;;) {
                character = fgetc(stream);
                if (character == KR16_EOF) {
                    break;
                }
                if (!scan_is_space(character)) {
                    ungetc(character, stream);
                    break;
                }
            }
            character = fgetc(stream);
            if (character == 43 || character == 45) {
                negative = character == 45;
                character = fgetc(stream);
            }
            if (character == KR16_EOF) {
                va_end(args);
                return assigned == 0 ? KR16_EOF : assigned;
            }
            for (;;) {
                digit = scan_digit_value(character, base);
                if (digit < 0) {
                    break;
                }
                total = total * base + digit;
                ++count;
                if (width > 0 && count >= width) {
                    character = KR16_EOF;
                    break;
                }
                character = fgetc(stream);
            }
            if (character != KR16_EOF) {
                ungetc(character, stream);
            }
            if (count == 0) {
                break;
            }
            if (negative) {
                total = -total;
            }
            if (!suppress) {
                int *target = va_arg(args, int *);
                *target = total;
                ++assigned;
            }
            ++format;
            continue;
        }
        if (*format == 99) {
            character = fgetc(stream);
            if (character == KR16_EOF) {
                va_end(args);
                return assigned == 0 ? KR16_EOF : assigned;
            }
            if (!suppress) {
                char *target = va_arg(args, char *);
                *target = (char)character;
                ++assigned;
            }
            ++format;
            continue;
        }
        if (*format == 115) {
            char *target;
            int count;
            for (;;) {
                character = fgetc(stream);
                if (character == KR16_EOF) {
                    va_end(args);
                    return assigned == 0 ? KR16_EOF : assigned;
                }
                if (!scan_is_space(character)) {
                    ungetc(character, stream);
                    break;
                }
            }
            target = suppress ? 0 : va_arg(args, char *);
            count = 0;
            for (;;) {
                character = fgetc(stream);
                if (character == KR16_EOF || scan_is_space(character)) {
                    if (character != KR16_EOF) {
                        ungetc(character, stream);
                    }
                    break;
                }
                if (target != 0) {
                    *target = (char)character;
                    ++target;
                }
                ++count;
                if (width > 0 && count >= width) {
                    break;
                }
            }
            if (target != 0) {
                *target = 0;
                ++assigned;
            }
            ++format;
            continue;
        }
        if (*format == 37) {
            character = fgetc(stream);
            if (character != 37) {
                if (character != KR16_EOF) {
                    ungetc(character, stream);
                }
                break;
            }
            ++format;
            continue;
        }
        break;
    }
    va_end(args);
    return assigned;
}

int remove(const char *path) {
    struct sys_unlink_args args;
    int status;
    if (path == 0) {
        errno = 22;
        return KR16_EOF;
    }
    args.path = (char *)path;
    status = sys_unlink(&args);
    set_errno_status(status);
    return status == SYS_OK ? 0 : KR16_EOF;
}

int rename(const char *old_path, const char *new_path) {
    struct sys_rename_args args;
    int status;
    if (old_path == 0 || new_path == 0) {
        errno = 22;
        return KR16_EOF;
    }
    args.old_path = (char *)old_path;
    args.new_path = (char *)new_path;
    status = sys_rename(&args);
    set_errno_status(status);
    return status == SYS_OK ? 0 : KR16_EOF;
}

















