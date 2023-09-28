#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include <shell/errcodes.h>
#include <storage/storage.h>
#include <storage/lfs.h>

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

FILE *__wrap_fopen64(const char *filename, const char *mode) {
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

    cookie_io_functions_t cookie_funcs;
    cookie_funcs.read = __sread;
    cookie_funcs.write = __swrite;
    cookie_funcs.seek = __sseek;
    cookie_funcs.close = __sclose;
    FILE *fp = fopencookie(file, mode, cookie_funcs);
    if (fp == NULL) {
        lfs_file_close(&lfs, file);
        free(file);
        errno = EIO;
        return NULL;
    }

    return fp;
}

FILE *__wrap_fopen(const char *filename, const char *mode) {
    return __wrap_fopen64(filename, mode);
}

FILE *__wrap_freopen64(const char *filename, const char *mode, FILE *stream) {
    (void)filename;
    (void)mode;
    return stream;
}

FILE *__wrap_freopen(const char *filename, const char *mode, FILE *stream) {
    return __wrap_freopen64(filename, mode, stream);
}

FILE *__wrap_tmpfile(void) {
    return NULL;
}

FILE *__wrap_tmpfile64(void) {
    return NULL;
}

int __wrap_remove(const char *fname) {
    if (lfs_remove(&lfs, fname) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int __wrap_rename(const char *old_filename, const char *new_filename) {
    if (lfs_rename(&lfs, old_filename, new_filename) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}
