#include "amanda.h"

int
main(int argc, char **argv) {
   int count;
   int debug = 0;
   char *name = NULL;
   int res = 0;

   while( ++argv, --argc ) {
       if (0 == strncmp( *argv, "-d", 2)) {
	   debug++;
           fprintf(stderr, "debug mode!\n");
       }
       if (0 == strncmp( *argv, "-f", 3) || 0 == strncmp( *argv, "-t", 3)) {
	   name = *(argv+1);
           debug && fprintf(stderr, "picked %s\n", name);
           argv++; argc--;
       }
       if (0 == strcmp(*argv, "rewind")) {
	   if (name == 0) {
		fprintf(stderr, "am-mt: -f device or -t device is required\n");
                exit(1);
           } else {
 		debug && printf("calling rewind\n");
		res = tape_rewind(name);
		if ( res ) {
                    fprintf(stderr, "rewind returns: %s\n", res);
                    exit(1);
		}
           }
       }
       if (0 == strcmp(*argv, "unload") || 0 == strcmp(*argv, "offline")) {
	   if (name == 0) {
		fprintf(stderr, "am-mt: -f device or -t device is required\n");
                exit(1);
           } else {
 		debug && printf("calling unload\n");
		res =  tape_unload(name);
		if ( res ) {
                    fprintf(stderr, "unload returns: %s\n", res);
                    exit(1);
		}
           }
       }
       if (0 == strncmp(*argv, "stat", 4)) {
	   if (name == 0) {
		fprintf(stderr, "am-mt: -f device or -t device is required\n", res);
                exit(1);
           } else {
 		debug && printf("calling status\n");
		res = tape_status(name);
		if ( res ) {
                    fprintf(stderr, "status returns: %s\n", res);
                    exit(1);
		}
           }
       }
       if (0 == strcmp(*argv, "fsf")) {
           int count;
	   count = atoi(*(argv+1));
	   if (name == 0) {
		fprintf(stderr, "am-mt: -f device or -t device is required\n");
                exit(1);
           } else {
 		debug && printf("calling fsf\n");
		res = tape_fsf(name, count);
		if ( res ) {
                    fprintf(stderr, "fsf returns: %s\n", res);
                    exit(1);
		}
           }
       }
   }
}
