
#define DEFAULT_SORT_ORDER      "hkdlb"

typedef struct find_result_s {
    struct find_result_s *next;
    int datestamp;
    int datestamp_aux;
        /* aux is secondary key for intra-day comparisons -- could be timestamp,
           just use seq# */
    char *hostname;
    char *diskname;
    int level;
    char *label;
    int  filenum;
    char *status;
} find_result_t;

find_result_t *find_dump P((char *find_hostname, int find_ndisks, char **find_diskstrs));
char **find_log P((void));
void sort_find_result P((char *sort_order, find_result_t **output_find));
void print_find_result P((find_result_t *output_find));
void free_find_result P((find_result_t **output_find));
find_result_t *dump_exist P((find_result_t *output_find, char *hostname, char *diskname, int datestamp, int level));

