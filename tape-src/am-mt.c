#include "amanda.h"
#include "tapeio.h"

int
main(int argc, char **argv) {
   int debug = 0;
   char *name = NULL;
   char *res = NULL;

   while( ++argv, --argc ) {
       if (0 == strncmp( *argv, "-d", 2)) {
	   debug++;
           fprintf(stderr, "debug mode!\n");
       }
       if (0 == strncmp( *argv, "-f", 3) || 0 == strncmp( *argv, "-t", 3)) {
	   name = *(argv+1);
           if(debug) fprintf(stderr, "picked %s\n", name);
           argv++; argc--;
       }
       if (0 == strcmp(*argv, "rewind")) {
	   if (name == 0) {
		fprintf(stderr, "am-mt: -f device or -t device is required\n");
                exit(1);
           } else {
 		if(debug) printf("calling rewind\n");
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
 		if(debug) printf("calling unload\n");
		res =  tape_unload(name);
		if ( res ) {
                    fprintf(stderr, "unload returns: %s\n", res);
                    exit(1);
		}
           }
       }
       if (0 == strncmp(*argv, "stat", 4)) {
	   if (name == 0) {
		fprintf(stderr, "am-mt: -f device or -t device is required: %s\n", res);
                exit(1);
           } else {
 		if(debug) printf("calling status\n");
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
 		if(debug) printf("calling fsf\n");
		res = tape_fsf(name, count);
		if ( res ) {
                    fprintf(stderr, "fsf returns: %s\n", res);
                    exit(1);
		}
           }
       }
   }
   return 0;
}
