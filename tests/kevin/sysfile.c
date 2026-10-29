#include <kr16/systemservices.h>
#include <stdio.h>

int main(void) {
    struct sys_mkdir_args mkdir_args;
    struct sys_open_args open_args;
    struct sys_open_result open_result;
    struct sys_close_args close_args;
    struct sys_readdir_args readdir_args;
    struct sys_readdir_result readdir_result;
    struct sys_stat_args stat_args;
    struct sys_stat stat_buffer;
    struct sys_fsinfo_args fsinfo_args;
    struct sys_fsinfo fsinfo_buffer;
    struct sys_time_args time_args;
    struct sys_time_result time_result;
    struct sys_random_args random_args;
    struct sys_random_result random_result;
    struct sys_dirent entries[4];
    FILE *file;

    mkdir_args.path = "dir";
    {
        int status = sys_mkdir(&mkdir_args);
        if (status != SYS_OK && status != SYS_EEXIST) return 1;
    }

    file = fopen("dir/data.txt", "w");
    if (file == 0) return 2;
    if (fputs("abc", file) != 0) return 3;
    if (fclose(file) != 0) return 4;

    stat_args.path = "dir/data.txt";
    stat_args.buffer = &stat_buffer;
    if (sys_stat(&stat_args) != SYS_OK) return 5;
    if ((stat_buffer.mode & SYS_S_IFREG) == 0) return 6;
    if (stat_buffer.size.lo != 3 || stat_buffer.size.hi != 0) return 7;

    open_args.path = "dir";
    open_args.flags = SYS_O_RDONLY;
    open_args.mode = 0;
    if (sys_open(&open_args, &open_result) != SYS_OK) return 8;
    readdir_args.fd = open_result.fd;
    readdir_args.buffer = entries;
    readdir_args.count = 4;
    if (sys_readdir(&readdir_args, &readdir_result) != SYS_OK) return 9;
    if (readdir_result.count != 1) return 10;
    if (entries[0].name[0] != 'd') return 11;
    close_args.fd = open_result.fd;
    if (sys_close(&close_args) != SYS_OK) return 12;

    fsinfo_args.buffer = &fsinfo_buffer;
    if (sys_fsinfo(&fsinfo_args) != SYS_OK) return 13;
    if (fsinfo_buffer.quota.lo == 0 && fsinfo_buffer.quota.hi == 0) return 14;

    time_args.reserved = 0;
    if (sys_time(&time_args, &time_result) != SYS_OK) return 15;
    random_args.reserved = 0;
    if (sys_random(&random_args, &random_result) != SYS_OK) return 16;
    if (remove("dir/data.txt") != 0) return 17;
    return 0;
}
