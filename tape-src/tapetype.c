/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: tapetype.c,v 1.11 2002/11/26 22:53:24 martinea Exp $
 *
 * tests a tape in a given tape unit and prints a tapetype entry for
 * it.  */
#include "amanda.h"

#include "tapeio.h"

#define NBLOCKS 32			/* number of random blocks */

static char *sProgName;
static char *tapedev;
static int fd;

static int blockkb = 32;
static int blocksize;

static char *randombytes = (char *) NULL;

#if USE_RAND
/* If the C library does not define random(), try to use rand() by
   defining USE_RAND, but then make sure you are not using hardware
   compression, because the low-order bits of rand() may not be that
   random... :-( */
#define random() rand()
#define srandom(seed) srand(seed)
#endif

static void initnotrandombytes() {
  int i, j;
  char *p;

  j =NBLOCKS * blocksize;
  if (randombytes == (char *)NULL) {
    randombytes = alloc(j);
  }
  p = randombytes;
  for(i=0; i < j; ++i) {
    *p++ = (char) (i % 256);
  }
}

static void initrandombytes() {
  int i, j;
  char *p;

  j = NBLOCKS * blocksize;
  if (randombytes == (char *)NULL) {
    randombytes = alloc(j);
  }
  p = randombytes;
  for(i=0; i < j; ++i) {
    *p++ = (char)random();
  }
}

static char *getrandombytes() {
  static int counter = 0;

  return randombytes + ((counter++ % NBLOCKS) * blocksize);
}

static int short_write;

int writeblock(fd)
     int fd;
{
  size_t w;

  if ((w = tapefd_write(fd, getrandombytes(), blocksize)) == blocksize) {
    return 1;
  }
  if (w >= 0) {
    short_write = 1;
  } else {
    short_write = 0;
  }
  return 0;
}


/* returns number of blocks actually written */
size_t writeblocks(int fd, size_t nblks)
{
  size_t blks = 0;

  while (blks < nblks) {
    if (! writeblock(fd)) {
      return 0;
    }
    blks++;
  }

  return blks;
}


void usage()
{
  fputs("usage: ", stderr);
  fputs(sProgName, stderr);
  fputs(" -h", stderr);
  fputs(" [-b blocksize]", stderr);
  fputs(" [-e estsize]", stderr);
  fputs(" [-f tapedev]", stderr);
  fputs(" [-t typename]", stderr);
  fputc('\n', stderr);
}

void help()
{
  usage();
  fputs("\
  -h			display this message\n\
  -b blocksize		record block size (default: 32k)\n\
  -e estsize		estimated tape size (default: 1g == 1024m)\n\
  -f tapedev		tape device name (default: $TAPE)\n\
  -t typename		tapetype name (default: unknown-tapetype)\n\
\n\
Note: disable hardware compression when running this program.\n\
", stderr);
}


int do_tty;

void show_progress(blocks, files)
  size_t *blocks, *files;
{
  fprintf(stderr, "wrote %ld %dKb block%s in %ld file%s",
	  (long)*blocks, blockkb, (*blocks == 1) ? "" : "s",
	  (long)*files, (*files == 1) ? "" : "s");
}


void do_pass(size, blocks, files, seconds)
  size_t size, *blocks, *files;
  time_t *seconds;
{
  size_t blks;
  time_t start, end;
  int save_errno;

  if (tapefd_rewind(fd) == -1) {
    fprintf(stderr, "%s: could not rewind %s: %s\n",
	    sProgName, tapedev, strerror(errno));
    exit(1);
  }

  time(&start);

  while(1) {

    if ((blks = writeblocks(fd, size)) <= 0 || tapefd_weof(fd, 1) != 0)
      break;
    *blocks += blks;
    (*files)++;
    if(do_tty) {
      putc('\r', stderr);
      show_progress(blocks, files);
    }
  }
  save_errno = errno;

  time(&end);

  if (*blocks == 0) {
    fprintf(stderr, "%s: could not write any data in this pass: %s\n",
	    sProgName, short_write ? "short write" : strerror(save_errno));
    exit(1);
  }

  if(end <= start) {
    /*
     * Just in case time warped backward or the device is really, really
     * fast (e.g. /dev/null testing).
     */
    *seconds = 1;
  } else {
    *seconds = end - start;
  }
  if(do_tty) {
    putc('\r', stderr);
  }
  show_progress(blocks, files);
  fprintf(stderr, " in %ld second%s (%s)\n",
	  (long)*seconds, ((long)*seconds == 1) ? "" : "s",
	  short_write ? "short write" : strerror(save_errno));
}


void do_pass0(size, seconds)
  size_t size;
  time_t *seconds;
{
  size_t blks;
  time_t start, end;
  int save_errno;

  if (tapefd_rewind(fd) == -1) {
    fprintf(stderr, "%s: could not rewind %s: %s\n",
	    sProgName, tapedev, strerror(errno));
    exit(1);
  }

  time(&start);

  blks = writeblocks(fd, size);
  tapefd_weof(fd, 1);

  save_errno = errno;

  time(&end);

  if (blks <= 0) {
    fprintf(stderr, "%s: could not write any data in this pass: %s\n",
	    sProgName, short_write ? "short write" : strerror(save_errno));
    exit(1);
  }

  if(end <= start) {
    /*
     * Just in case time warped backward or the device is really, really
     * fast (e.g. /dev/null testing).
     */
    *seconds = 1;
  } else {
    *seconds = end - start;
  }
}


int main(argc, argv)
     int argc;
     char *argv[];
{
  size_t pass1blocks = 0;
  size_t pass2blocks = 0;
  time_t pass1time;
  time_t pass2time;
  time_t timediff;
  size_t pass1files = 0;
  size_t pass2files = 0;
  size_t estsize;
  size_t pass0size;
  size_t pass1size;
  size_t pass2size;
  size_t blockdiff;
  size_t filediff;
  long filemark;
  long speed;
  size_t size;
  char *sizeunits;
  int ch;
  char *suffix;
  char *typename;
  time_t now;
  int hwcompr = 0;

  if ((sProgName = strrchr(*argv, '/')) == NULL) {
    sProgName = *argv;
  } else {
    sProgName++;
  }

  estsize = 1024 * 1024;			/* assume 1 GByte for now */
  tapedev = getenv("TAPE");
  typename = "unknown-tapetype";

  while ((ch = getopt(argc, argv, "b:e:f:t:h")) != EOF) {
    switch (ch) {
    case 'b':
      blockkb = strtol(optarg, &suffix, 0);
      if (*suffix == '\0' || *suffix == 'k' || *suffix == 'K') {
      } else if (*suffix == 'm' || *suffix == 'M') {
	blockkb *= 1024;
      } else if (*suffix == 'g' || *suffix == 'G') {
	blockkb *= 1024 * 1024;
      } else {
	fprintf(stderr, "%s: unknown size suffix \'%c\'\n", sProgName, *suffix);
	return 1;
      }
      break;
    case 'e':
      estsize = strtol(optarg, &suffix, 0);
      if (*suffix == '\0' || *suffix == 'k' || *suffix == 'K') {
      } else if (*suffix == 'm' || *suffix == 'M') {
	estsize *= 1024;
      } else if (*suffix == 'g' || *suffix == 'G') {
	estsize *= 1024 * 1024;
      } else {
	fprintf(stderr, "%s: unknown size suffix \'%c\'\n", sProgName, *suffix);
	return 1;
      }
      break;
    case 'f':
      tapedev = optarg;
      break;
    case 't':
      typename = optarg;
      break;
    case 'h':
      help();
      return 1;
      break;
    default:
      fprintf(stderr, "%s: unknown option \'%c\'\n", sProgName, ch);
      /* fall through to ... */
    case '?':
      usage();
      return 1;
      break;
    }
  }
  blocksize = blockkb * 1024;

  if (tapedev == NULL || optind < argc) {
    usage();
    return 1;
  }

  fd = tape_open(tapedev, O_RDWR);
  if (fd == -1) {
    fprintf(stderr, "%s: could not open %s: %s\n",
	    sProgName, tapedev, strerror(errno));
    return 1;
  }

  do_tty = isatty(fileno(stderr));

  /*
   * Estimate pass: write twice a file of 16 MByte, once with compressable
   * data and once with uncompressable data.
   * The theory is that if the drive is in hardware compression mode
   * we notice a significant difference in writing speed between the two
   * (at least if we can provide data as fast the tape streams).
   */

  initnotrandombytes();

  pass0size = 16 * 1024 / blockkb;  /* 16 MB default -- make this tuneable? */

  printf("Estimate phase 1...\n");

  do_pass0(pass0size, &pass1time);

  /*
   * now generate uncompressable data and try again
   */
  time(&now);
  srandom(now);
  initrandombytes();

  printf("Estimate phase 2...\n");
  do_pass0(pass0size, &pass2time);

  /*
   * Compute the time difference between writing the compressable and
   * uncompressable data.  If it differs more than 20%, then warn
   * user that the tape drive has probably hardware compression enabled.
   */
  if (pass1time > pass2time) {
    timediff = pass1time - pass2time;
  } else {
    timediff = pass2time - pass1time;
  }
  if (((100 * timediff) / pass2time) >= 20) {	/* 20% faster? */
    printf("WARNING: The tape drive has hardware compression enabled\n");
    hwcompr = 1;
  }

  /*
   * Inform about estimated time needed to run the remaining of this program
   */
  printf("Estimated time to write 2 * %d Mbyte: ", estsize / 1024);
  pass1time = 2 * pass2time * estsize / (16 * 1024);
  printf("%ld sec = ", (long)pass1time);
  printf("%ld h %ld min\n", ((long)pass1time / 3600),
	 (((long)pass1time % 3600) / 60));

  /*
   * Do pass 1 -- write files that are 1% of the estimated size until error.
   */
  pass1size = (estsize * 0.01) / blockkb;	/* 1% of estimate */
  if(pass1size <= 0) {
    pass1size = 2;				/* strange end case */
  }
  do_pass(pass1size, &pass1blocks, &pass1files, &pass1time);

  /*
   * Do pass 2 -- write smaller files until error.
   */
  pass2size = pass1size / 2;
  do_pass(pass2size, &pass2blocks, &pass2files, &pass2time);

  /*
   * Compute the size of a filemark as the difference in data written
   * between pass 1 and pass 2 divided by the difference in number of
   * file marks written between pass 1 and pass 2.  Note that we have
   * to be careful in case size_t is unsigned (i.e. do not subtract
   * things and then check for less than zero).
   */
  if (pass1blocks <= pass2blocks) {
    /*
     * If tape marks take up space, there should be fewer blocks in pass
     * 2 than in pass 1 since we wrote twice as many tape marks.  But
     * odd things happen, so make sure the result does not go negative.
     */
    blockdiff = 0;
  } else {
    blockdiff = pass1blocks - pass2blocks;
  }
  if (pass2files <= pass1files) {
    /*
     * This should not happen, but just in case ...
     */
    filediff = 1;
  } else {
    filediff = pass2files - pass1files;
  }
  filemark = blockdiff * blockkb / filediff;

  /*
   * Compute the length as the average of the two pass sizes including
   * tape marks.
   */
  size = ((pass1blocks * blockkb + filemark * pass1files)
           + (pass2blocks * blockkb + filemark * pass2files)) / 2;
  if (size >= 1024 * 1024 * 1000) {
    size /= 1024 * 1024;
    sizeunits = "gbytes";
  } else if (size >= 1024 * 1000) {
    size /= 1024;
    sizeunits = "mbytes";
  } else {
    sizeunits = "kbytes";
  }

  /*
   * Compute the speed as the average of the two passes.
   */
  speed = (((double)pass1blocks * blockkb / pass1time)
           + ((double)pass2blocks * blockkb / pass2time)) / 2;

  /*
   * Dump the tapetype.
   */
  printf("define tapetype %s {\n", typename);
  printf("    comment \"just produced by tapetype prog (hardware compression %s)\"\n",
	hwcompr ? "on" : "off");
  printf("    length %ld %s\n", (long)size, sizeunits);
  printf("    filemark %ld kbytes\n", filemark);
  printf("    speed %ld kps\n", speed);
  printf("}\n");

  if (tapefd_rewind(fd) == -1) {
    fprintf(stderr, "%s: could not rewind %s: %s\n",
	    sProgName, tapedev, strerror(errno));
    return 1;
  }

  if (tapefd_close(fd) == -1) {
    fprintf(stderr, "%s: could not close %s: %s\n",
	    sProgName, tapedev, strerror(errno));
    return 1;
  }

  return 0;
}
