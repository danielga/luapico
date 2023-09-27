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
static int __sflags(const char *restrict mode, int *optr) {
	int ret = 0, m = 0, o = 0;
	switch (*mode++) {
	case 'r':	/* open for reading */
		ret = __SRD;
		m = LFS_O_RDONLY;
		o = 0;
		break;

	case 'w':	/* open for writing */
		ret = __SWR;
		m = LFS_O_WRONLY;
		o = LFS_O_CREAT | LFS_O_TRUNC;
		break;

	case 'a':	/* open for appending */
		ret = __SWR;
		m = LFS_O_WRONLY;
		o = LFS_O_CREAT | LFS_O_APPEND;
		break;

	default:	/* illegal mode */
		errno = EINVAL;
		return 0;
	}

	/* [rwa]\+ or [rwa]b\+ means read and write */
	if (*mode == '+' || (*mode == 'b' && mode[1] == '+')) {
		ret = __SRW;
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

FILE *__wrap_fopen( const char *restrict filename, const char *restrict mode ) {
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

FILE *__wrap_freopen( const char *filename, const char *mode, FILE *restrict stream ) {
    return stream;
}

FILE *__wrap_tmpfile( void ) {
    return NULL;
}

int __wrap_remove( const char *fname ) {
    if (lfs_remove(&lfs, fname) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int __wrap_rename( const char *old_filename, const char *new_filename ) {
    if (lfs_rename(&lfs, old_filename, new_filename) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

/*
int	__wrap_fclose( FILE *stream ) {
    int err = lfs_file_close(&lfs, (lfs_file_t *)stream->_cookie);
    free(stream->_cookie);
    free(stream);

    if (err != 0) {
        errno = EIO;
        return EOF;
    }

    return 0;
}

int __wrap_fflush( FILE *stream ) {
    if (lfs_file_sync(&lfs, stream->_cookie) != 0) {
        stream->_flags |= __SERR;
        errno = EIO;
        return EOF;
    }

    return 0;
}

size_t __wrap_fread( void *restrict buffer, size_t size, size_t count, FILE *restrict stream ) {
    size_t total = size * count;
    lfs_ssize_t read = lfs_file_read(&lfs, (lfs_file_t *)stream->_cookie, buffer, total);
    if (read < 0) {
        stream->_flags |= __SERR;
        errno = EIO;
        return 0;
    } else if ((size_t)read != total) {
        stream->_flags |= __SEOF;
    }

    return (size_t)read;
}

int	__wrap_getc_unlocked( FILE *stream ) {
    unsigned char c = 0;
    lfs_ssize_t read = lfs_file_read(&lfs, (lfs_file_t *)stream->_cookie, &c, 1);
    if (read != 1) {
        if (read == 0)
            stream->_flags |= __SEOF;
        else
            stream->_flags |= __SERR;

        errno = EIO;
        return EOF;
    }

    return (int)c;
}

void __wrap_flockfile( FILE * ) {}

void __wrap_funlockfile( FILE * ) {}

int __wrap_getc( FILE *stream ) {
    return __wrap_getc_unlocked(stream);
}

int __wrap_fprintf( FILE *stream, const char *format, ... ) {
    int len = 0;
    {
        va_list args;
        va_start(args, format);
        len = vsnprintf(NULL, 0, format, args);
        va_end(args);
        if (len <= 0) {
            stream->_flags |= __SERR;
            return len;
        }
    }

    char buffer[len + 1];
    {
        va_list args;
        va_start(args, format);
        len = vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (len <= 0) {
            stream->_flags |= __SERR;
            return len;
        }
    }

    lfs_ssize_t written = lfs_file_write(&lfs, (lfs_file_t *)stream->_cookie, buffer, len);
    if (written < 0 || written != (lfs_ssize_t)len) {
        stream->_flags |= __SERR;
        return -1;
    }

    return len;
}

size_t __wrap_fwrite( const void *restrict buffer, size_t size, size_t count, FILE *restrict stream ) {
    size_t total = size * count;
    lfs_ssize_t written = lfs_file_write(&lfs, (lfs_file_t *)stream->_cookie, buffer, total);
    if (written < 0 || written != (lfs_ssize_t)total) {
        stream->_flags |= __SERR;
        return EOF;
    }

    return 0;
}

int __wrap_ungetc( int ch, FILE *stream ) {
    if (lfs_file_seek(&lfs, (lfs_file_t *)stream->_cookie, -1, LFS_SEEK_CUR) < 0) {
        stream->_flags |= __SERR;
        errno = EIO;
        return EOF;
    }

    stream->_flags &= ~__SERR;
    return ch;
}

int __wrap_fseeko( FILE *stream, off_t offset, int whence ) {
    lfs_soff_t off = lfs_file_seek(&lfs, (lfs_file_t *)stream->_cookie, (lfs_soff_t)offset, whence);
    if (off < 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

off_t __wrap_ftello( FILE *stream ) {
    lfs_soff_t offset = lfs_file_tell(&lfs, (lfs_file_t *)stream->_cookie);
    if (offset < 0) {
        errno = EIO;
        return -1;
    }

    return (off_t)offset;
}

int __wrap_fseek( FILE *stream, long offset, int origin ) {
    lfs_soff_t off = lfs_file_seek(&lfs, (lfs_file_t *)stream->_cookie, (lfs_soff_t)offset, origin);
    if (off < 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

long __wrap_ftell( FILE *stream ) {
    lfs_soff_t offset = lfs_file_tell(&lfs, (lfs_file_t *)stream->_cookie);
    if (offset < 0) {
        errno = EIO;
        return -1;
    }

    return (long)offset;
}

int __wrap_setvbuf( FILE *, char *, int, size_t ) {
    errno = ENOTSUP;
    return -1;
}

char *__wrap_fgets( char *restrict str, int count, FILE *restrict stream ) {
    lfs_ssize_t res = lfs_file_read(&lfs, (lfs_file_t *)stream->_cookie, str, count);
    if (res < 0) {
        stream->_flags |= __SERR;
        errno = EIO;
        return NULL;
    } else if (res != (lfs_ssize_t)count) {
        stream->_flags |= __SEOF;

        if (res == 0)
            return NULL;
    }

    return str;
}

int __wrap_fputs( const char *restrict str, FILE *restrict stream ) {
    size_t count = strlen(str);
    lfs_ssize_t res = lfs_file_write(&lfs, (lfs_file_t *)stream->_cookie, str, count);
    if (res < 0 || res != (lfs_ssize_t)count) {
        stream->_flags |= __SERR;
        return EOF;
    }

    return 1;
}

int __wrap_ferror( FILE *stream ) {
    return (stream->_flags & __SERR) != 0;
}

int __wrap_feof( FILE *stream ) {
    return (stream->_flags & __SEOF) != 0;
}

void __wrap_clearerr( FILE *stream ) {
    stream->_flags &= ~(__SERR|__SEOF);
}
*/
