/*
 *	$Id: libscsi.h,v 1.1 1998/01/24 06:46:20 amcore Exp $
 *
 *	libscsi.h -- library header for routines to handle the changer
 *			support for chio based systems
 *
 *	Author: Eric Schnoebelen, eric@cirr.com
 *	based on work by: Larry Pyeatt,  pyeatt@cs.colostate.edu 
 *	Copyright: 1997, Eric Schnoebelen
 *		
 */

/*
 * the name of the calling program
 * (an amanda convention)
 */
extern char *pname;

/* 
 * is the specified slot empty?
 */
int isempty(int fd, int slot);

/*
 * find the first empty slot 
 */
int find_empty(int fd);

/*
 * returns one if there is a tape loaded in the drive 
 */
int drive_loaded(int fd, int drivenum);


/*
 * unloads the drive, putting the tape in the specified slot 
 */
int unload(int fd, int drive, int slot);

/*
 * moves tape from the specified slot into the drive 
 */
int load(int fd, int drive, int slot);

/* 
 * return the number of slots in the robot
 */
int get_slot_count(int fd);

/*
 * return the number of drives in the robot
 */
int get_drive_count(int fd);

