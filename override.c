#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>


// Created by Vitaly "_Vi" Shukela; 2013; License=MIT

/* I want only O_CREAT|O_WRONLY|O_TRUNC, not queer open's or creat's signatures */
//#include <fcntl.h>
#define _FCNTL_H
#include <bits/fcntl.h>
#define AT_FDCWD -100


static int absolutize_path(char *outpath, int outpath_s, const char* pathname, int dirfd) {
    if(pathname[0]!='/') {
        int l;
        // relative path
        if (dirfd == AT_FDCWD) {
            char* ret = getcwd(outpath, outpath_s);
            if (!ret) {
                return -1;
            }
            l = strlen(outpath);
            if (l>=outpath_s) {
                errno=ERANGE;
                return -1;
            }
        } else {
            char proce[128];
            sprintf(proce, "/proc/self/fd/%d", dirfd);
            
            ssize_t ret = readlink(proce, outpath, outpath_s);
            
            if( ret==-1) {
                /* Let's just assume dirfd is for "/" (as in /bin/rm) */
                fprintf(stderr, "Warning: can't readlink %s, continuing\n", proce);
                l=0;
            } else {
                l=ret;
            }

        }
        outpath[l]='/';
        strncpy(outpath+l+1, pathname, outpath_s-l-1);
    } else {
        // absolute path
        snprintf(outpath, outpath_s, "%s", pathname);
    }
    return 0;
}


static int remote_openat(int dirfd, const char *pathname, int flags, mode_t mode);

static int remote_open(const char *pathname, int flags, mode_t mode) {
    return remote_openat(AT_FDCWD, pathname, flags, mode);
}

static int remote_open64(const char *pathname, int flags, mode_t mode) {
    return remote_open(pathname, flags, mode); }
static int remote_openat64(int dirfd, const char *pathname, int flags, mode_t mode) {
    return remote_openat(dirfd, pathname, flags, mode); }
static int remote_creat(const char *pathname, mode_t mode) { 
    return remote_open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode); }
static int remote_creat64(const char *pathname, mode_t mode) { 
    return remote_open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode); }


/* Taken from musl-0.9.7 */
static int __fmodeflags(const char *mode)
{
        int flags;
        if (strchr(mode, '+')) flags = O_RDWR;
        else if (*mode == 'r') flags = O_RDONLY;
        else flags = O_WRONLY;
        if (strchr(mode, 'x')) flags |= O_EXCL;
        if (strchr(mode, 'e')) flags |= O_CLOEXEC;
        if (*mode != 'r') flags |= O_CREAT;
        if (*mode == 'w') flags |= O_TRUNC;
        if (*mode == 'a') flags |= O_APPEND;
        return flags;
}



static FILE* remote_fopen(const char *path, const char *mode) {
    int flags = __fmodeflags(mode);
    int ret = remote_open(path, flags|O_LARGEFILE, 0666);
    if (ret==-1) return NULL;
    return fdopen(ret, mode);
}

static FILE* remote_fopen64(const char *path, const char *mode) {
    return remote_fopen(path, mode);
}


#define OVERIDE_TEMPLATE_I(name, rettype, succcheck, signature, sigargs) \
    /* static rettype (*orig_##name) signature = NULL; */ \
    rettype name signature { \
        return remote_##name sigargs; \
    }

#define OVERIDE_TEMPLATE(name, signature, sigargs) \
    OVERIDE_TEMPLATE_I(name, int, ret!=-1, signature, sigargs)


OVERIDE_TEMPLATE(open, (const char *pathname, int flags, mode_t mode), (pathname, flags, mode))
OVERIDE_TEMPLATE(open64, (const char *pathname, int flags, mode_t mode), (pathname, flags, mode))
OVERIDE_TEMPLATE(openat, (int dirfd, const char *pathname, int flags, mode_t mode), (dirfd, pathname, flags, mode))
OVERIDE_TEMPLATE(openat64, (int dirfd, const char *pathname, int flags, mode_t mode), (dirfd, pathname, flags, mode))
OVERIDE_TEMPLATE(creat, (const char *pathname,  mode_t mode), (pathname, mode))
OVERIDE_TEMPLATE(creat64, (const char *pathname, mode_t mode), (pathname, mode))

OVERIDE_TEMPLATE_I(fopen, FILE*, ret != NULL, (const char *path, const char *mode), (path, mode))
OVERIDE_TEMPLATE_I(fopen64, FILE*, ret != NULL, (const char *path, const char *mode), (path, mode))


#define MAXPATH 4096
#define MAXOVERRIDES 128

static int is_initialized = 0;
static char paths_from[MAXPATH][MAXOVERRIDES];
static char paths_to[MAXPATH][MAXOVERRIDES];
static int n_overrides = 0;
static int do_absolutize = 1;
static int do_debug = 0;


static void initialize() {
    const char* e;
    e = getenv("FOPEN_OVERRIDE");
    if(!e) {
        fprintf(stderr, "Usage: LD_PRELOAD=libfopen_override.so FOPEN_OVERRIDE=/path1/file1=/overridden/path/file1,/qqq/file2=/root/file2.dat program [argguments...]\n");
        fprintf(stderr, "    Additional flags (instead of 'file=file' pair): debug, noabs\n");
        fprintf(stderr, "    Use backslash to escape commas and equal signs\n");
        return;
    }

    char buffer[MAXPATH];
    int buffer_level;
    int its_key_now = 1;
    for(;;) {
        if (n_overrides >= MAXOVERRIDES) {
            fprintf(stderr, "fopen_override Maximum overrides count exceed\n");
            return;
        }

        switch (*e) {
            case '=':
                if (its_key_now) {
                    its_key_now = 0;
                    buffer[buffer_level] = 0;
                    snprintf(paths_from[n_overrides], MAXPATH, "%s", buffer);
                    buffer_level = 0;
                } else {
                    fprintf(stderr, "fopen_override Repeated unescaped '=' in a chunk\n");
                    return;
                }
                break;
            case ',':
            case '\0':
                if (its_key_now) {
                    buffer[buffer_level] = 0;
                    if (!strcmp(buffer, "")) {

                    } else
                    if (!strcmp(buffer, "debug")) {
                        do_debug = 1;
                    } else 
                    if (!strcmp(buffer, "noabs" )) {
                        do_absolutize = 0;
                    } else {
                        fprintf(stderr, "fopen_override No '=' in a chunk and the chunk is no 'debug' or 'noabs'\n");
                        return;
                    }
                    buffer_level = 0;
                    its_key_now = 1;
                } else {
                    its_key_now = 1;
                    buffer[buffer_level] = 0;
                    snprintf(paths_to[n_overrides], MAXPATH, "%s", buffer);
                    buffer_level = 0;
                    if (do_debug) fprintf(stderr, "fopen_override Replacement: %s -> %s\n", paths_from[n_overrides], paths_to[n_overrides]);
                    ++n_overrides;
                }
                break;
            case '\\':
                ++e;
                // intentional fallthough
            default:
                buffer[buffer_level] = *e;
                ++buffer_level;
                if (buffer_level >= MAXPATH-1) {
                    fprintf(stderr, "fopen_override Exceed maximum string length\n");
                    return;
                }
        }
        if (!*e) break;
        ++e;
    }

    is_initialized = 1;
}

int (*orig_openat) (int dirfd, const char *pathname, int flags, mode_t mode) = NULL;
static int remote_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (!is_initialized) initialize();
    if (!is_initialized) return -1;

    const char* pn = pathname;
    if (do_debug) fprintf(stderr, "fopen_override file=%s dirfd=%d flags=%d mode=%d orinsym=%p\n", pn, dirfd, flags, mode, orig_openat);

    char pathbuf[MAXPATH];
    if (do_absolutize) {
        
        if (absolutize_path(pathbuf, sizeof pathbuf, pathname, dirfd) == -1) return -1;

        pn = pathbuf;

        if (do_debug) fprintf(stderr, "fopen_override abs=%s\n", pn);
    } 
    int i;
    for (i=0; i<n_overrides; ++i) {
        if(!strcmp(pn, paths_from[i])) {
            pn = paths_to[i];
            if (do_debug) fprintf(stderr, "fopen_override overridden=%s\n", pn);
            break;
        }
    }

    if(!orig_openat) {
        orig_openat = dlsym(RTLD_NEXT, "openat");
    }
    
    return (*orig_openat)(dirfd, pn, flags, mode);
}
