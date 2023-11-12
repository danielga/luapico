#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <shell/errcodes.h>
#include <storage/storage.h>
#include <storage/lfs.h>

#include "array.h"

#define STDIO_HANDLE_STDIN 0
#define STDIO_HANDLE_STDOUT 1
#define STDIO_HANDLE_STDERR 2
#define STDIO_HANDLE_MAX STDIO_HANDLE_STDERR

#define STDIO_MAX_FD 64

#define WRAPPER(result, name, parameters) extern result __real_##name parameters; \
result __wrap_##name parameters

typedef struct fs_node {
    uint8_t type;
    union {
        lfs_file_t file;
        lfs_dir_t dir;
    };
} fs_node_t;

array_define(fs_node_t, STDIO_MAX_FD);
static array_type(fs_node_t, STDIO_MAX_FD) files = array_create(fs_node_t, STDIO_MAX_FD);

extern lfs_t lfs;

/*	$OpenBSD: flags.c,v 1.6 2005/08/08 08:05:36 espie Exp $ */
/*
 * Return the (stdio) flags for a given mode.  Store the flags
 * to be passed to an open() syscall through *optr.
 * Return 0 on error.
 */
static int __sflags(const char *mode, int *optr) {
    int ret = 0, m = 0, o = 0;
    switch (*mode++) {
    case 'r':	/* open for reading */
        ret = 0x0004;
        m = LFS_O_RDONLY;
        o = 0;
        break;

    case 'w':	/* open for writing */
        ret = 0x0008;
        m = LFS_O_WRONLY;
        o = LFS_O_CREAT | LFS_O_TRUNC;
        break;

    case 'a':	/* open for appending */
        ret = 0x0008;
        m = LFS_O_WRONLY;
        o = LFS_O_CREAT | LFS_O_APPEND;
        break;

    default:	/* illegal mode */
        errno = EINVAL;
        return 0;
    }

    /* [rwa]\+ or [rwa]b\+ means read and write */
    if (*mode == '+' || (*mode == 'b' && mode[1] == '+')) {
        ret = 0x0010;
        m = LFS_O_RDWR;
    }

    *optr = m | o;
    return ret;
}

static ssize_t __sread(void *cookie, char *buf, size_t n) {
    lfs_file_t *file = (lfs_file_t *)cookie;
    lfs_ssize_t read = lfs_file_read(&lfs, file, buf, n);
    if (read < 0) {
        return -1;
    }

    return read;
}

static ssize_t __swrite(void *cookie, const char *buf, size_t n) {
    lfs_file_t *file = (lfs_file_t *)cookie;
    lfs_ssize_t written = lfs_file_write(&lfs, file, buf, n);
    if (written < 0) {
        return 0;
    }

    return written;
}

static int __sseek(void *cookie, off_t *offset, int whence) {
    lfs_file_t *file = (lfs_file_t *)cookie;
    lfs_soff_t off = lfs_file_seek(&lfs, file, (lfs_soff_t)*offset, whence);
    if (off < 0) {
        return -1;
    }

    *offset = (off_t)off;
    return 0;
}

static int __sclose(void *cookie) {
    lfs_file_t *file = (lfs_file_t *)cookie;
    int err = lfs_file_close(&lfs, file);
    free(file);
    return err >= 0 ? 0 : EOF;
}

WRAPPER(FILE *, fopen64, (const char *filename, const char *mode)) {
    int mode_flags = 0;
    int flags = __sflags(mode, &mode_flags);
    if (flags == 0) {
        errno = EINVAL;
        return NULL;
    }

    lfs_file_t *file = (lfs_file_t *)calloc(1, sizeof(lfs_file_t));
    if (file == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    int err = lfs_file_open(&lfs, file, filename, mode_flags);
    if (err != 0) {
        free(file);
        errno = EIO;
        return NULL;
    }

    cookie_io_functions_t cookie_funcs = {
        .read = __sread,
        .write = __swrite,
        .seek = __sseek,
        .close = __sclose
    };
    FILE *fp = fopencookie(file, mode, cookie_funcs);
    if (fp == NULL) {
        lfs_file_close(&lfs, file);
        free(file);
        errno = EIO;
        return NULL;
    }

    return fp;
}

WRAPPER(FILE *, fopen, (const char *filename, const char *mode)) {
    return __wrap_fopen64(filename, mode);
}

WRAPPER(FILE *, freopen64, (const char *filename, const char *mode, FILE *stream)) {
    (void)filename;
    (void)mode;
    return stream;
}

WRAPPER(FILE *, freopen, (const char *filename, const char *mode, FILE *stream)) {
    return __wrap_freopen64(filename, mode, stream);
}

WRAPPER(FILE *, tmpfile, (void)) {
    return NULL;
}

WRAPPER(FILE *, tmpfile64, (void)) {
    return NULL;
}

WRAPPER(int, remove, (const char *fname)) {
    if (lfs_remove(&lfs, fname) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

WRAPPER(int, rename, (const char *old_filename, const char *new_filename)) {
    if (lfs_rename(&lfs, old_filename, new_filename) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

WRAPPER(int, open, (const char *filename, int flags, ...)) {
    printf("open\n");

    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap = { 0 };
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }

    fs_node_t *file = (fs_node_t *)array_lock_value(&files);
    if (file == NULL) {
        errno = ENOMEM;
        return -1;
    }

    int err = lfs_file_open(&lfs, &file->file, filename, flags);
    if (err != 0) {
        array_free_value(&files, file);
        errno = EIO;
        return -1;
    }

    return array_value_position(file);
}

WRAPPER(int, close, (int fd)) {
    printf("close\n");

    fs_node_t *file = (fs_node_t *)array_value_from_position(&files, (size_t)fd);
    if (file == NULL) {
        errno = EINVAL;
        return -1;
    }

    int err = lfs_file_close(&lfs, &file->file);
    array_free_value(&files, file);
    if (err != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

WRAPPER(int, read, (int handle, void *buffer, size_t length)) {
    printf("read\n");

    if (handle == STDIO_HANDLE_STDIN) {
        return __real_read(handle, buffer, length);
    }

    if (handle == STDIO_HANDLE_STDOUT || handle == STDIO_HANDLE_STDERR) {
        return -1;
    }

    fs_node_t *file = (fs_node_t *)array_value_from_position(&files, (size_t)handle);
    if (file == NULL) {
        errno = EINVAL;
        return -1;
    }

    lfs_ssize_t read = lfs_file_read(&lfs, &file->file, buffer, length);
    if (read < 0) {
        errno = read;
        return -1;
    }

    return read;
}

WRAPPER(int, write, (int handle, const void *buffer, size_t length)) {
    printf("write\n");

    if (handle == STDIO_HANDLE_STDOUT || handle == STDIO_HANDLE_STDERR) {
        return __real_write(handle, buffer, length);
    }

    if (handle == STDIO_HANDLE_STDIN) {
        return -1;
    }

    fs_node_t *file = (fs_node_t *)array_value_from_position(&files, (size_t)handle);
    if (file == NULL) {
        errno = EINVAL;
        return -1;
    }

    lfs_ssize_t written = lfs_file_write(&lfs, &file->file, buffer, length);
    if (written < 0) {
        errno = written;
        return -1;
    }

    return written;
}

WRAPPER(off_t, lseek, (int fd, off_t pos, int whence)) {
    printf("lseek\n");

    fs_node_t *file = (fs_node_t *)array_value_from_position(&files, (size_t)fd);
    if (file == NULL) {
        errno = EINVAL;
        return -1;
    }

    lfs_soff_t off = lfs_file_seek(&lfs, &file->file, (lfs_soff_t)pos, whence);
    if (off < 0) {
        errno = off;
        return -1;
    }

    return off;
}

WRAPPER(int, fstat, (__unused int fd, __unused struct stat *buf)) {
    printf("fstat\n");
    errno = ENOTSUP;
    return -1;
}
