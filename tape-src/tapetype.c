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
 * $Id: tapetype.c,v 1.3.2.3.2.2 2001/07/11 21:39:36 jrjackson Exp $
 *
 * tests a tape in a given tape unit and prints a tapetype entry for
 * it.  */
#include "amanda.h"

#include "tapeio.h"

#define BLOCKKB 32			/* block size in KBytes */
#define BLOCKSIZE ((BLOCKKB) * 1024)	/* block size in bytes */
#define NBLOCKS 32			/* number of random blocks */

static char *sProgName;
static char *tapedev;
static int fd;

static char randombytes[NBLOCKS][BLOCKSIZE];

#if USE_RAND
/* If the C library does not define random(), try to use rand() by
   defining USE_RAND, but then make sure you are not using hardware
   compression, because the low-order bits of rand() may not be that
   random... :-( */
#define random() rand()
#define srandom(seed) srand(seed)
#endif

static void initrandombytes() {
  int i, j;
  for(i=0; i < NBLOCKS; ++i)
    for(j=0; j < BLOCKSIZE; ++j)
      randombytes[i][j] = (char)random();
}

static char *getrandombytes() {
  static int counter = 0;
  ++counter;
  return randombytes[counter % NBLOCKS];
}

static int short_write;

int writeblock(fd)
     int fd;
{
  size_t w;

  if ((w = tapefd_write(fd, getrandombytes(), BLOCKSIZE)) == BLOCKSIZE) {
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
	  (long)*blocks, BLOCKKB, (*blocks == 1) ? "" : "s",
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


int main(argc, argv)
     int argc;
     char *argv[];
{
  size_t pass1blocks = 0;
  size_t pass2blocks = 0;
  time_t pass1time;
  time_t pass2time;
  size_t pass1files = 0;
  size_t pass2files = 0;
  size_t estsize;
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

  if ((sProgName = strrchr(*argv, '/')) == NULL) {
    sProgName = *argv;
  } else {
    sProgName++;
  }

  estsize = 1024 * 1024;			/* assume 1 GByte for now */
  tapedev = getenv("TAPE");
  typename = "unknown-tapetype";

  while ((ch = getopt(argc, argv, "e:f:t:h")) != EOF) {
    switch (ch) {
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

  time(&now);
  srandom(now);
  initrandombytes();

  do_tty = isatty(fileno(stderr));

  /*
   * Do pass 1 -- write files that are 1% of the estimated size until error.
   */
  pass1size = (estsize * 0.01) / BLOCKKB;	/* 1% of estimate */
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
  filemark = blockdiff * BLOCKKB / filediff;

  /*
   * Compute the length as the average of the two pass sizes including
   * tape marks.
   */
  size = ((pass1blocks * BLOCKKB + filemark * pass1files)
           + (pass2blocks * BLOCKKB + filemark * pass2files)) / 2;
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
  speed = (((double)pass1blocks * BLOCKKB / pass1time)
           + ((double)pass2blocks * BLOCKKB / pass2time)) / 2;

  /*
   * Dump the tapetype.
   */
  printf("define tapetype %s {\n", typename);
  printf("    comment \"just produced by tapetype program\"\n");
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
