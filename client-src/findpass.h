/*
 * findpass.h - interface to findpass module.
 */
#ifndef FINDPASS_H
#define FINDPASS_H

#include "amanda.h"

extern char *findpass P((char *disk, char *pass));
extern char *makesharename P((char *disk, char *buffer, int shell));

#endif

