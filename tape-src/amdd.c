#ifdef NO_AMANDA
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "output-rait.h"

#define	tape_open	rait_open
#define	tapefd_read	rait_read
#define	tapefd_write	rait_write
#define tapefd_setinfo_length(outfd, length)
#define	tapefd_close	rait_close

#else
#include "amanda.h"
#include "tapeio.h"
#endif

static int debug = 0;
static char *pgm = NULL;

static void
usage()
{
    fprintf(stderr, "usage: %s ", pgm);
    fprintf(stderr, " [-d]");
    fprintf(stderr, " [-l length]");
    fprintf(stderr, " [if=input]");
    fprintf(stderr, " [of=output]");
    fprintf(stderr, " [bs=blocksize]");
    fprintf(stderr, " [count=count]");
    fprintf(stderr, " [skip=count]");
    fprintf(stderr, "\n");
    exit(1);
}

int
main(int argc, char **argv) {
    int infd = 0;				/* stdin */
    int outfd = 1;				/* stdout */
    int blocksize = 512;
    int skip=0;
    int len;
    int pread, fread, pwrite, fwrite;
    int res = 0;
    char *buf;
    int count = 0;
    int have_count = 0;
    int save_errno;
    int ch;
    char *eq;
    int length = 0;
    int have_length = 0;
    int (*read_func)();
    int (*write_func)();

    if((pgm = strrchr(argv[0], '/')) != NULL) {
	pgm++;
    } else {
	pgm = argv[0];
    }
    while(-1 != (ch = getopt(argc, argv, "hdl:"))) {
	switch(ch) {
	case 'd':
	    debug = 1;
	    fprintf(stderr, "debug mode!\n");
	    break;
	case 'l':
	    have_length = 1;
	    length = atoi(optarg);
	    len = strlen(optarg);
	    if(len > 0) {
		switch(optarg[len-1] ) {
		case 'k':				break;
		case 'b': length /= 2;	 		break;
		case 'M': length *= 1024;		break;
		default:  length /= 1024;		break;
		}
	    } else {
		length /= 1024;
	    }
	    break;
	case 'h':
	default:
	    usage();
	    /* NOTREACHED */
	}
    }

    read_func = read;
    write_func = write;
    for( ; optind < argc; optind++) {
	if(0 == (eq = strchr(argv[optind], '='))) {
	    usage();
	    /* NOTREACHED */
	}
	len = eq - argv[optind];
	if(0 == strncmp("if", argv[optind], len)) {
	    if((infd = tape_open(eq + 1, O_RDONLY)) < 0) {
		save_errno = errno;
		fprintf(stderr, "%s: %s: ", pgm, eq + 1);
		errno = save_errno;
		perror("open");
		return 1;
	    }
	    read_func = tapefd_read;
            if(debug) {
		fprintf(stderr, "input opened \"%s\", got fd %d\n",
				eq + 1, infd);
	    }
	} else if(0 == strncmp("of", argv[optind], len)) {
	    if((outfd = tape_open(eq + 1, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
		save_errno = errno;
		fprintf(stderr, "%s: %s: ", pgm, eq + 1);
		errno = save_errno;
		perror("open");
		return 1;
	    }
	    write_func = tapefd_write;
            if(debug) {
		fprintf(stderr, "output opened \"%s\", got fd %d\n",
				eq + 1, outfd);
	    }
	    if(have_length) {
		if(debug) {
		    fprintf(stderr, "length set to %d\n", length);
		}
		tapefd_setinfo_length(outfd, length);
	    }
	} else if(0 == strncmp("bs", argv[optind], len)) {
	    blocksize = atoi(eq + 1);
	    len = strlen(argv[optind]);
	    if(len > 0) {
		switch(argv[optind][len-1] ) {
		case 'k': blocksize *= 1024;		break;
		case 'b': blocksize *= 512; 		break;
		case 'M': blocksize *= 1024 * 1024;	break;
		}
	    }
	    if(debug) {
		fprintf(stderr, "blocksize set to %d\n", blocksize);
	    }
	} else if(0 == strncmp("count", argv[optind], len)) {
	    count = atoi(eq + 1);
	    have_count = 1;
	    if(debug) {
		fprintf(stderr, "count set to %d\n", count);
	    }
	} else if(0 == strncmp("skip", argv[optind], len)) {
	    skip = atoi(eq + 1);
	    if(debug) {
		fprintf(stderr, "skip set to %d\n", skip);
	    }
	} else {
	    fprintf(stderr, "%s: bad argument: \"%s\"\n", pgm, argv[optind]);
	    return 1;
	}
    }

    if(0 == (buf = malloc(blocksize))) {
	save_errno = errno;
	fprintf(stderr, "%s: ", pgm);
	errno = save_errno;
	perror("malloc error");
	return 1;
    }

    eq = "read error";
    pread = fread = pwrite = fwrite = 0;
    while(0 < (len = (*read_func)(infd, buf, blocksize))) {
	if(skip-- > 0) {
	    continue;
	}
	if(len == blocksize) {
	    fread++;
	} else if(len > 0) {
	    pread++;
	}
	len = (*write_func)(outfd, buf, len);
	if(len < 0) {
	    eq = "write error";
	    break;
	} else if(len == blocksize) {
	    fwrite++;
	} else if(len > 0) {
	    pwrite++;
	}
	if(have_count) {
	    if(--count <= 0) {
		len = 0;
		break;
	    }
	}
    }
    if(len < 0) {
	save_errno = errno;
	fprintf(stderr, "%s: ", pgm);
	errno = save_errno;
	perror(eq);
	res = 1;
    }
    fprintf(stderr, "%d+%d in\n%d+%d out\n", fread, pread, fwrite, pwrite);
    if(read_func == tapefd_read) {
	if(0 != tapefd_close(infd)) {
	    save_errno = errno;
	    fprintf(stderr, "%s: ", pgm);
	    errno = save_errno;
	    perror("input close");
	    res = 1;
	}
    }
    if(write_func == tapefd_write) {
	if(0 != tapefd_close(outfd)) {
	    save_errno = errno;
	    fprintf(stderr, "%s: ", pgm);
	    errno = save_errno;
	    perror("output close");
	    res = 1;
	}
    }
    return res;
}
