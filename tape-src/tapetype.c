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
 * $Id: tapetype.c,v 1.3 1998/09/02 03:40:54 oliva Exp $
 *
 * tests a tape in a given tape unit and prints a tapetype entry for
 * it.  */
#include "amanda.h"

#include "tapeio.h"

#define BLOCKSIZE (32768)
#define NBLOCKS (32)

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
  static int counter;
  ++counter;
  return randombytes[counter % NBLOCKS];
}

int writeblock(fd)
     int fd;
{
  return write(fd, getrandombytes(), BLOCKSIZE) == BLOCKSIZE;
}

int main(argc, argv)
     int argc;
     char *argv[];
{
  int minblocks = 0, maxblocks = 0;
  int fd;
  time_t start, end;

  if (argc != 3) {
    fprintf(stderr, "usage: tapetype NAME DEV\n");
    return 1;
  }

  fd = tape_open(argv[2], O_RDWR);
  if (fd == -1) {
    fprintf(stderr, "could not open %s: %s\n", argv[2], strerror(errno));
    return 1;
  }

  if (tapefd_rewind(fd) == -1) {
    fprintf(stderr, "could not rewind %s: %s\n", argv[2], strerror(errno));
    return 1;
  }

  initrandombytes();

  time(&start);

  srandom(start);

  while(writeblock(fd))
    if (++maxblocks % 100 == 0)
      fprintf(stderr, "\rwrote %d 32Kb blocks", maxblocks);

  time(&end);

  fprintf(stderr, "\rwrote %d 32Kb blocks in %ld seconds\n",
	  maxblocks, (long)end-start);

  if (tapefd_rewind(fd) == -1) {
    fprintf(stderr, "could not rewind %s: %s\n", argv[2], strerror(errno));
    return 1;
  }

  while(writeblock(fd) && tapefd_weof(fd, 1) == 0)
    if (++minblocks % 100 == 0)
      fprintf(stderr, "\rwrote %d 32Kb sections", minblocks);

  fprintf(stderr, "\rwrote %d 32Kb sections\n", minblocks);

  printf("define tapetype %s {\n", argv[1]);
  printf("    comment \"just produced by tapetype program\"\n");
  printf("    length %d mbytes\n", maxblocks/32);
  printf("    filemark %d kbytes\n", (maxblocks-minblocks)*32/minblocks);
  printf("    speed %d kbytes\n", maxblocks*32/(end-start));
  printf("}\n");

  if (tapefd_rewind(fd) == -1) {
    fprintf(stderr, "could not rewind %s: %s\n", argv[2], strerror(errno));
    return 1;
  }

  if (tapefd_close(fd) == -1) {
    fprintf(stderr, "could not close %s: %s\n", argv[2], strerror(errno));
    return 1;
  }

  return 0;
}
