#include "amanda.h"

typedef struct generic_fs_stats {
    long total;		/* total KB in filesystem */
    long avail;		/* KB available to non-superuser */
    long free;		/* KB free for superuser */

    long files;		/* total # of files in filesystem */
    long favail;	/* # files avail for non-superuser */
    long ffree;		/* # files free for superuser */
} generic_fs_stats_t;

int get_fs_stats P((char *dir, generic_fs_stats_t *sp));
