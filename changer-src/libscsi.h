/*
 *	$Id: libscsi.h,v 1.2 1998/02/24 03:28:52 blair Exp $
 *
 *	libscsi.h -- library header for routines to handle the changer
 *			support for chio based systems
 *
 *	Author: Eric Schnoebelen, eric@cirr.com
 *	based on work by: Larry Pyeatt,  pyeatt@cs.colostate.edu 
 *	Copyright: 1997, Eric Schnoebelen
 *		
 */

#ifndef LIBSCSI_H
#define LIBSCSI_H

#include "amanda.h"

/*
 * the name of the calling program
 * (an amanda convention)
 */
extern char *pname;

/* 
 * is the specified slot empty?
 */
int isempty P((int fd, int slot));

/*
 * find the first empty slot 
 */
int find_empty P((int fd));

/*
 * returns one if there is a tape loaded in the drive 
 */
int drive_loaded P((int fd, int drivenum));


/*
 * unloads the drive, putting the tape in the specified slot 
 */
int unload P((int fd, int drive, int slot));

/*
 * moves tape from the specified slot into the drive 
 */
int load P((int fd, int drive, int slot));

/* 
 * return the number of slots in the robot
 */
int get_slot_count P((int fd));

/*
 * return the number of drives in the robot
 */
int get_drive_count P((int fd));

#endif	/* !LIBSCSI_H */
