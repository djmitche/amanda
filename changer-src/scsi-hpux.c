/*
 *	$Id: scsi-hpux.c,v 1.3 1998/05/27 08:14:24 amcore Exp $
 *
 *	scsi-chio.c -- library routines to handle the changer
 *			support for chio based systems
 *
 *	Author: Eric Schnoebelen, eric@cirr.com
 *	interface based on work by: Larry Pyeatt, pyeatt@cs.colostate.edu 
 *	Copyright: 1997, 1998 Eric Schnoebelen
 *		
 */

#include "config.h"
#include "amanda.h"

#if defined(HAVE_HPUX_SCSI)
# include <sys/scsi.h>

char *moddesc = "@(#)" __FILE__
		": HP/UX SCSI changer support routines @(#)";

/* 
 * a cache of the robotics information, for use elsewhere
 */
static struct element_addresses changer_info;
static int changer_info_init = 0;

static int get_changer_info(fd)
{
int rc = 0;

    if (!changer_info_init) {
	rc = ioctl(fd, SIOC_ELEMENT_ADDRESSES, &changer_info);
	changer_info_init++;
    }
    return (rc);
}

/* 
 * this routine checks a specified slot to see if it is empty 
 */
int isempty(int fd, int slot)
{
struct element_status es;
int rc;

    /*
     * fill the cache as required
     */
    get_changer_info(fd);

    es.element = changer_info.first_storage + slot;

    rc = ioctl(fd, SIOC_ELEMENT_STATUS, &es);
    if (rc) {
	fprintf(stderr, "%s: element status query failed: 0x%x %s\n",
				get_pname(), rc, strerror(errno));
	return(-1);
    }

    return !es.full;
}

/*
 * find the first empty slot 
 */
int find_empty(int fd)
{
struct element_status  es;
int i, rc;

    get_changer_info(fd);

    i = changer_info.first_storage;
    do {
	es.element = i++;
	rc = ioctl(fd, SIOC_ELEMENT_STATUS, &es);
    } while ( (i <= (changer_info.first_storage + changer_info.num_storages))
		&& !rc && es.full);

    if (rc) {
	fprintf(stderr,"%s: element status query failed: 0x%x %s\n",
				get_pname(), rc, strerror(errno));
	return -1;
    }
    return (i - changer_info.first_storage - 1);
}

/*
 * returns one if there is a tape loaded in the drive 
 */
int drive_loaded(int fd, int drivenum)
{
struct element_status  es;
int                            i,rc;

    get_changer_info(fd);

    es.element = changer_info.first_data_transfer + drivenum;

    rc = ioctl(fd, SIOC_ELEMENT_STATUS, &es);
    if (rc) {
	fprintf(stderr,"%s: drive status quer failed: 0x%x %s\n",
				get_pname(), rc, strerror(errno));
	return(-1);
    }

    return es.full;
}


/*
 * unloads the drive, putting the tape in the specified slot 
 */
int unload(int fd, int drive, int slot)
{
struct move_medium_parms  move;
int rc;

    get_changer_info(fd);

    /*
     * pick the first transport, just for simplicity
     */
    move.transport = changer_info.first_transport;

    move.source = changer_info.first_data_transfer + drive;
    move.destination = changer_info.first_storage + slot;
    move.invert = 0;

    rc = ioctl(fd, SIOC_MOVE_MEDIUM, &move);
    if (rc){
	fprintf(stderr,"%s: move medium command failed: 0x%x %s\n",
		get_pname(), rc, strerror(errno));
	return(-2);
    }
    return 0;
}


/*
 * moves tape from the specified slot into the drive 
 */
int load(int fd, int drive, int slot)
{
struct move_medium_parms  move;
int rc;

    get_changer_info(fd);

    /*
     * use the first transport defined in the changer, for lack of a
     * better choice..
     */
    move.transport = changer_info.first_transport;

    move.source = changer_info.first_storage + slot;
    move.destination = changer_info.first_data_transfer + drive;
    move.invert = 0;

    rc = ioctl(fd, SIOC_MOVE_MEDIUM,&move);
    if (rc){
	fprintf(stderr,"%s: drive load failed (MOVE): 0x%x %s\n",
		get_pname(), rc, strerror(errno));
	return(-2);
    }
    return (0);
}

int get_slot_count(int fd)
{ 
int rc;

    rc = get_changer_info(fd);
    if (rc) {
        fprintf(stderr, "%s: storage size query failed: 0x%x %s\n", get_pname(),
						rc, strerror(errno));
        return -1;
    }

    return(changer_info.num_storages);

}

int get_drive_count(int fd)
{ 
    int rc;

    rc = get_changer_info(fd);
    if (rc) {
        fprintf(stderr, "%s: drive count query failed: 0x%x %s\n", get_pname(),
						rc, strerror(errno));
        return -1;
    }

    return changer_info.num_data_transfers;
}

#endif
