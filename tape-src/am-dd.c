#include "amanda.h"
#include "tapeio.h"

int
main(int argc, char **argv) {
   int infd = 0;
   int outfd = 1;
   int  blocksize=512;
   int  skip=0;
   int len;
   int pread, fread, pwrite, fwrite;
   int res = 0;
   char * buf;
   int count;
   int debug = 0;

   count = -1;
   while( ++argv, --argc ) {
       if (0 == strncmp( *argv, "-d", 2)) {
	   debug++;
           fprintf(stderr, "debug mode!\n");
       }
       if (0 == strncmp( *argv, "skip=", 5)) {
	  skip = atoi((*argv)+5);
       }
       if (0 == strncmp( *argv, "if=", 3)) {
	   infd = tape_open((*argv)+3, O_RDONLY);
	   if (infd < 0) {
               perror("open");
               exit(2);
           }
           if(debug)
	       fprintf(stderr, "input opened %s, got fd %d\n", *argv+3, infd);
       }
       if (0 == strncmp( *argv, "of=", 3)) {
	   outfd = tape_open((*argv)+3, O_RDWR|O_CREAT|O_TRUNC );
	   if (outfd < 0) {
               perror("open");
               exit(2);
           }
           if(debug)
	       fprintf(stderr, "output opened %s, got fd %d\n", *argv+3, outfd);
       }

       if (0 == strncmp( *argv, "bs=", 3)) {
	  blocksize = atoi((*argv)+3);
          len = strlen(*argv);
          switch( (*argv)[len-1] ) {
          case 'k': blocksize *= 1024; 		break;
          case 'b': blocksize *= 512; 		break;
          case 'M': blocksize *= 1024 * 1024;	break;
          }
          if(debug) fprintf(stderr, "got blocksize %d\n", blocksize);
       }
       if (0 == strncmp( *argv, "count=", 6)) {
	  count = atoi(*argv+6);
          if(debug) fprintf(stderr, "got count %d\n", count);
       }
   }
   pread = fread = pwrite = fwrite = 0;
   buf = sbrk(blocksize);
   while( 0 < (len = tapefd_read(infd, buf, blocksize))) {
       if ( skip > 0 ) {
	   skip--;
	   continue;
       }
       if (len < blocksize) {
	   pread++;
       } else {
           fread++;
       }
       len = tapefd_write(outfd, buf, len);
       if (len < blocksize) {
	   if (len > 0) {
	      pwrite++;
           }
       } else {
           fwrite++;
       }
       if (len < 0) {
	   perror("dd: write error");
           res = 1;
	   len = 0; 
	   break;
       }
       if (count > 0) {
	  count--;
	  if (count == 0) {
	       len = 0;
               break;
 	  }
       }
   }
   if (len < 0) {
       perror("dd: read error");
       res = 1;
   }
   fprintf(stderr, "%d+%d in\n%d+%d out\n", fread, pread, fwrite, pwrite);
   return res;
}
