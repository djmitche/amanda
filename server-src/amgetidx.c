/* $RCSfile: amgetidx.c,v $
   $Date: 1997/04/27 21:10:52 $

   amgetidx - gets index files from clients

   Dr Alan M. McIvor
*/

#include "amanda.h"
#include "arglist.h"
#include <netinet/in_systm.h>
#include "conffile.h"
#include "diskfile.h"
#include "version.h"
#include "infofile.h"
#include "logfile.h"
#include "stream.h"
#include "clock.h"
#include "protocol.h"
#include "arglist.h"

#define READ_TIMEOUT	30*60
#define MAX_LINE 	1024
#define DATABUF_SIZE 	32*1024

#define STARTUP_TIMEOUT		   60

char *pname = "amgetidx";

static char databuf[DATABUF_SIZE];
static char errstr[256];
static char *dataptr;		/* data buffer markers */

static int datafd;
static int amanda_port;
static int mesgfd = -1;

/* these global variables define the currently being collected index */
static char progname[1024];
static char hostname[1024], diskname[1024];
static int level;
static char datestamp[80];

#ifdef KRB4_SECURITY
int kamanda_port;
int krb4_auth;
int kencrypt;
CREDENTIALS cred;
#endif
    
static void service_ports_init()
{
    struct servent *amandad;

    if((amandad = getservbyname("amanda", "udp")) == NULL) {
	amanda_port = AMANDA_SERVICE_DEFAULT;
	log(L_WARNING, "no amanda/udp service, using default port %d",
	    AMANDA_SERVICE_DEFAULT);
    }
    else
	amanda_port = ntohs(amandad->s_port);

#ifdef KRB4_SECURITY
    if((amandad = getservbyname("kamanda", "udp")) == NULL) {
	kamanda_port = KAMANDA_SERVICE_DEFAULT;
	log(L_WARNING, "no kamanda/udp service, using default port %d",
	    KAMANDA_SERVICE_DEFAULT);
    }
    else
	kamanda_port = ntohs(amandad->s_port);
#endif
}

static void construct_datestamp(buf, pastdays)
char *buf;
int pastdays;
{
    struct tm *tm;
    time_t timestamp;

    timestamp = time((time_t *)NULL);
    timestamp -= pastdays*24*60*60;
    tm = localtime(&timestamp);
    sprintf(buf, "%04d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
}

/*
 * Get the datestamp out of what the user specified.  Try to be flexible.
 * Return -1 if there was an error, 0 if the input was OK.
 */
static int get_datestamp(buf, src)
char *buf;
char *src;
{
    int i, j, l;
    int has_dash;

    l = strlen(src);

    /* get today's date to start with */
    construct_datestamp(buf, 0);

    has_dash = 0;
    for (i = 0; i < l; i++)
	if (src[i] == '-')
	{
	    has_dash = 1;
	    break;
	}

    if (has_dash)
    {
	switch (l)
	{
	    case 10:
		if ((src[4] != '-') || (src[7] != '-'))
		    return -1;
		j = 0;
		for (i = 0; i < 4; i++)
		    if (isdigit((int)(src[i])))
			datestamp[j++] = src[i];
		    else
			return -1;
		for (i = 5; i < 7; i++)
		    if (isdigit((int)(src[i])))
			datestamp[j++] = src[i];
		    else
			return -1;
		for (i = 8; i < 10; i++)
		    if (isdigit((int)(src[i])))
			datestamp[j++] = src[i];
		    else
			return -1;
		break;
	    case 7:
		if ((src[0] != '-') || (src[1] != '-') || (src[4] != '-'))
		    return -1;
		j = 4;
		for (i = 2; i < 4; i++)
		    if (isdigit((int)(src[i])))
			datestamp[j++] = src[i];
		    else
			return -1;
		for (i = 5; i < 7; i++)
		    if (isdigit((int)(src[i])))
			datestamp[j++] = src[i];
		    else
			return -1;
		break;
	    case 5:
		if ((src[0] != '-') || (src[1] != '-') || (src[2] != '-'))
		    return -1;
		j = 6;
		for (i = 3; i < 5; i++)
		    if (isdigit((int)(src[i])))
			datestamp[j++] = src[i];
		    else
			return -1;
		break;
	    default:
		return -1;
	}
    }
    else
    {
	/* non '-' format */
	if ((l != 2) && (l != 4) && (l != 8))
	    return -1;
	j = 8-l;
	for (i = 0; i < l; i++)
	    if (isdigit((int)(src[i])))
		datestamp[j++] = src[i];
	    else
		return -1;
    }

    return 0;
}

static int response_error;

static void sendindex_response(p, pkt)
proto_t *p;
pkt_t *pkt;
{
    int data_port, mesg_port;

    if(p->state == S_FAILED)
    {
	if(pkt == NULL)
	{
	    dbprintf(("[request timeout]"));
	    sprintf(errstr, "[request timeout]");
	    response_error = 1;
	    return;
	}
	else
	{
	    dbprintf(("got nak response:\n----\n%s----\n", pkt->body));
	    if(sscanf(pkt->body, "ERROR %[^\n]", errstr) != 1)
	    {
		dbprintf(("amgetidx: got strange NAK: %s", pkt->body));
		strcpy(errstr, "[request NAK]");
	    }
	    response_error = 2;
	    return;
	}
    }

#ifdef KRB4_SECURITY
    if(krb4_auth && !check_mutual_authenticator(&cred.session, pkt, p)) {
	sprintf(errstr, "[mutual-authentication failed]");
	response_error = 3;
	return;
    }
#endif

    if(!strncmp(pkt->body, "ERROR", 5))
    {
	/* this is an error response packet */
	if(sscanf(pkt->body, "ERROR %[^\n]", errstr) != 1)
	    sprintf(errstr, "[bogus error packet]");
	response_error = 4;
	return;
    }

    if(sscanf(pkt->body, "CONNECT DATA %d MESG %d\n", &data_port,
	&mesg_port) != 2)
    {
	sprintf(errstr, "[parse of reply message failed]");
	response_error = 5;
	return;
    }

    datafd = stream_client(hostname, data_port,
			   DEFAULT_SIZE, DEFAULT_SIZE);
    if(datafd == -1) {
	sprintf(errstr, 
		"[could not connect to data port: %s]", strerror(errno));
	response_error = 6;
	return;
    }
    mesgfd = stream_client(hostname, mesg_port,
                           DEFAULT_SIZE, DEFAULT_SIZE);
    if(mesgfd == -1) {
        sprintf(errstr,
                "[could not connect to mesg port: %s]", strerror(errno));
        close(datafd);  
        response_error = 1; 
        return;
    }
    /* everything worked */

#ifdef KRB4_SECURITY
    if(krb4_auth && kerberos_handshake(datafd, cred.session) == 0) {
	sprintf(errstr,
		"[mutual authentication in data stream failed]");
	close(datafd);
	close(mesgfd);
	response_error = 7;
	return;
    }
    if(krb4_auth && kerberos_handshake(mesgfd, cred.session) == 0) {
	sprintf(errstr,
		"[mutual authentication in mesg stream failed]");
	close(datafd);
	close(mesgfd);
	response_error = 8;
	return;
    }
#endif
    response_error = 0;
}

static int startup_retrieve P((void))
{
    char req[8192];
    int rc;

    sprintf(req,
	    "SERVICE sendindex PROGRAM %s\n%s %d DATESTAMP %s\n",
            progname, diskname, level, datestamp);

    dbprintf(("sending req ----------\n"));
    dbprintf(("%s", req));
    dbprintf(("sending req ----------\n"));

#ifdef KRB4_SECURITY
    if(krb4_auth) {
	rc = make_krb_request(hostname, kamanda_port, req, NULL, 
			      STARTUP_TIMEOUT, sendindex_response);
	if(!rc) {
	    char inst[256], realm[256];
#define HOSTNAME_INSTANCE inst
	    /* 
	     * This repeats a lot of work with make_krb_request, but it's
	     * ultimately the kerberos library's fault: krb_mk_req calls
	     * krb_get_cred, but doesn't make the session key available!
	     * XXX: But admittedly, we could restructure a bit here and
	     * at least eliminate the duplicate gethostbyname().
	     */
	    if(host2krbname(hostname, inst, realm) == 0)
		rc = -1;
	    else
		rc = krb_get_cred(CLIENT_HOST_PRINCIPLE, CLIENT_HOST_INSTANCE,
				  realm, &cred);
	}
	if(rc > 0) {
	    sprintf(errstr, "[host %s: krb4 error %d: %s]",
		    hostname, rc, krb_err_txt[rc]);
	    return 2;
	}
    }
    else
#endif
	rc = make_request(hostname, amanda_port, req, NULL,
			  STARTUP_TIMEOUT, sendindex_response);
    
    if (rc)
    {
        fprintf(stderr, "[could not resolve name \"%s\": error %d]", 
		hostname, rc);
	return -1;
    }
    run_protocol();

    return response_error;
}

/* write data out */
static int update_dataptr(outf, size)
int outf;
int size;
{
    int written;
    
    while (size > 0)
    {
	written = write(outf, dataptr, size);
	if (written == -1)
	    return 1;
	size -= written;
	dataptr += size;
    }
    return 0;
}


static void do_retreive(datafd, outfd)
int datafd;
int outfd;
{
    int maxfd, nfound, size1, eof1;
    fd_set readset, selectset;
    struct timeval timeout;
    int spaceleft;
    
    dataptr = databuf;
    spaceleft = DATABUF_SIZE;

#if 0
    NAUGHTY_BITS_INITIALIZE;
#endif
    
    maxfd = datafd + 1;
    eof1 = 0;

    FD_ZERO(&readset);

    timeout.tv_sec = READ_TIMEOUT;
    timeout.tv_usec = 0;
    FD_SET(datafd, &readset);

    while (!eof1) {

	memcpy(&selectset, &readset, sizeof(fd_set));

	nfound = select(maxfd, (SELECT_ARG_TYPE *)(&selectset), NULL,
			NULL, &timeout);

	/* check for errors or timeout */

	if(nfound == 0)  {
	    printf("data timeout");
	    return;
	}
	if(nfound == -1) {
	    printf("select: %s", strerror(errno));
	    return;
	}

	/* read/write any data */

	if(FD_ISSET(datafd, &selectset)) {
	    size1 = read(datafd, dataptr, spaceleft);
	    switch(size1) {
	    case -1: 
		sprintf(errstr, "data read: %s", strerror(errno));
		printf("FAILED amgetidx [%s]\n", errstr);
		return;
	    case 0:
		if(update_dataptr(outfd, size1)) return;
		eof1 = 1;
		close(datafd);
		FD_CLR(datafd, &readset);
		break;
	    default:
		if(update_dataptr(outfd, size1)) return;
	    }
	}
	
    } /* end while */

    return;
}

  
static void retrieve_index(outfd)
int outfd;
{
    int rc;
    
    rc = startup_retrieve();
    if (rc)
    {
	/* failed to startup index retreive */
	printf("amgetidx: failed to start up index retreive from %s: %s\n",
	       hostname, errstr);
	dbprintf(("failed to start up index retreive from %s: %s\n",
		  hostname, errstr));
	close(datafd);
	return;
    }

    do_retreive(datafd, outfd);
    close(datafd);

    return;
}

    
/* return the level of the dump on the given date */
/* return -1 if can't find it */
static int getlevel(config)
char *config;
{
    char date[1024];
    int lev;
    char tape[1024];
    int file;
    char cmd[1024];
    FILE *fp;
    int first_line = 0;
    char format[1024];
    int the_level;
    char iso_datestamp[1024];
    
    sprintf(cmd, "%s/amadmin%s %s find %s %s", bindir, versionsuffix(),
	    config, hostname, diskname);

    dbprintf(("cmd is \"%s\"\n", cmd));

    if ((fp = popen(cmd, "r")) == NULL)
    {
	dbprintf(("System error on popen: %d", errno));
	return -1;
    }
    sprintf(format, "%%s %s %s %%d %%s %%d", hostname, diskname);

    dbprintf(("format is \"%s\"\n", format));

    sprintf(iso_datestamp, "%c%c%c%c-%c%c-%c%c",
	    datestamp[0], datestamp[1], datestamp[2], datestamp[3],
	    datestamp[4], datestamp[5],
	    datestamp[6], datestamp[7]);
    dbprintf(("looking for datestamp \"%s\"\n", iso_datestamp));

    the_level = -1;
    while (fgets(cmd, 1024, fp) != NULL)
    {
	if (cmd[strlen(cmd)-1] == '\n')
	    cmd[strlen(cmd)-1] = '\0';

	dbprintf(("Read \"%s\"\n", cmd));
	
	if (first_line++ == 0)
	    continue;
	if (sscanf(cmd, format, date, &lev, tape, &file) != 4)
	    continue;			/* assume failed dump */

	dbprintf(("date=\"%s\", lev=\"%d\", tape=\"%s\", file=\"%d\"\n",
		  date, lev, tape, file));

	if (strcmp(date, iso_datestamp) == 0)
	{
	    the_level = lev;
	    break;
	}
    }
    pclose(fp);

    dbprintf(("the_level=%d\n", the_level));

    return the_level;
}



static void construct_idx_fname(indexfilename, conf_idx_dir)
char *indexfilename;
const char *conf_idx_dir;
{
    char *pc;

    /* construct the file name */
    sprintf(indexfilename, "%s/%s_%s_%c%c%c%c-%c%c-%c%c_%d%s",
	    conf_idx_dir, hostname, diskname,
	    datestamp[0], datestamp[1], datestamp[2], datestamp[3],
	    datestamp[4], datestamp[5], datestamp[6], datestamp[7],
	    level, COMPRESS_SUFFIX);

    /* map all spaces and /'s intp _'s */
    for (pc = indexfilename+strlen(conf_idx_dir)+1; *pc != '\0'; pc++)
	if ((*pc == '/') || (*pc == ' '))
	    *pc = '_';

    dbprintf(("Putting into '%s'\n", indexfilename));
}

static void usage (argv0)
const char *argv0;
{
    fprintf(stderr,
	    "Usage: %s <config> [<on datespec> || <ago numberdays>]\n",
	    argv0);
    fprintf(stderr,
	    "If you need to get index files from days other than the\n");
    fprintf(stderr,
	    "current day, you can specify the day you want in two ways.\n");
    fprintf(stderr,
	    "Use <on dataspec> if you know the date of the index files\n");
    fprintf(stderr,
	    "you want or use <ago numberdays> to get the index files\n");
    fprintf(stderr,
	    "that were generated nunberofdays days ago.  Without either\n");
    fprintf(stderr,
	    "the on or ago option, %s will get the current index files.\n",
	    argv0);
    exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
    dgram_t *msg;
    int protocol_port;
    char conf_idx_dir[1024];
    char indexfilename[1024];
    struct stat statbuf;
    int outfd;
    FILE *outfp;
    char cmd[2048];
    disk_t *diskp;
    disklist_t *diskl;
    
    if ((argc != 2) && (argc != 4))
	usage(argv[0]);

    /* get datestamp */
    if ( argc == 2 )
	construct_datestamp(datestamp, 0);
    else
    {
	if ( strcmp(argv[2], "on") && strcmp(argv[2], "ago"))
	    usage(argv[0]);
	
	if ( strcmp(argv[2], "on") )
	    construct_datestamp(datestamp, atoi(argv[3]));
	else
	{
	    if (get_datestamp(datestamp, argv[3]) == -1)
		error("Datestamp not one of: YYYYMMDD MMDD DD YYYY-MM-DD --MM-DD ---DD");
	}
    }

    dbopen("/tmp/amgetidx.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));

    /* set up dgram port first thing */
    msg = dgram_alloc();
    if(dgram_bind(msg, &protocol_port) == -1)
        error("could not bind result datagram port: %s",
	       strerror(errno));

    if(geteuid() == 0) {
	/* set both real and effective uid's to real uid, likewise for gid */
	setgid(getgid());
	setuid(getuid());
    }
#ifdef BSD_SECURITY
    else error("must be run setuid root to communicate correctly");
#endif

    dbprintf(("%s: pid %ld executable %s version %s, using port %d\n",
	      pname, (long) getpid(), argv[0], version(), protocol_port));

    service_ports_init();
    proto_init(msg->socket, time(0), 16);

    dbprintf(("Initialized ports.  Amanda_port=%d\n", amanda_port));

    /* get the list of disks being dumped and their types */
    sprintf(cmd, "%s/%s", CONFIG_DIR, argv[1]);
    if (chdir(cmd) != 0) {
	dbprintf(("%s: could not cd to confdir %s: %s\n", pname, cmd,
		  strerror(errno)));
	dbclose();
        error("could not cd to confdir %s: %s", cmd, strerror(errno));
    }
    if (read_conffile(CONFFILE_NAME)) {
	dbprintf(("could not read amanda config file."));
	dbclose();
        error("could not read amanda config file.");
    }
    if ((diskl = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL) {
	dbprintf(("could not load \"%s\".", getconf_str(CNF_DISKFILE)));
	dbclose();
        error("could not load \"%s\".", getconf_str(CNF_DISKFILE));
    }

    /* generate the name of config index directory where we should put
       things and check that it exists */
    sprintf(conf_idx_dir, "%s/%s", INDEX_DIR, argv[1]);
    if ((stat(conf_idx_dir, &statbuf) == -1)
	|| !S_ISDIR(statbuf.st_mode)
	|| (access(conf_idx_dir, W_OK) == -1))
    {
	dbprintf(("Index dir \"%s\" doesn't exist or is not writable.\n",
		  conf_idx_dir));
	dbprintf(("Correct this problem and rerun amgetidx.\n"));
	dbclose();
	error("Index dir \"%s\" doesn't exist or is not writable.",
	      conf_idx_dir);
	/*NOTREACHED*/
    }

    if (diskl == NULL)
    {
	dbprintf(("The disk list is empty.\n"));
	dbclose();
	error("The disk list is empty.");
	/*NOTREACHED*/
    }

    /* okay finished the preliminary stuff, let's handle the retreives */
    /* if problems with one, try the next */
    for (diskp = diskl->head; diskp != NULL; diskp = diskp->next)
    {
	if (diskp->dtype->index)
	{
	    /* put the details into global buffers */
	    strcpy(hostname, diskp->host->hostname);
	    strcpy(diskname, diskp->name);
	    strcpy(progname, diskp->dtype->program);
		
	    /* get the level of the dump */
	    if ((level = getlevel(argv[1])) == -1)
	    {
		fprintf(stderr,
			"amgetidx: couldn't get level for hostname='%s', diskname='%s', datestamp='%s'\n",
			hostname, diskname, datestamp);
		continue;
	    }

	    /* ready to get dump */
	    dbprintf(("retrieving hostname='%s', diskname='%s', datestamp='%s', level=%d\n",
		      hostname, diskname, datestamp, level));

	    /* get the filename for the index file */
	    construct_idx_fname(indexfilename, conf_idx_dir);

	    /* open a pipe through compress to index file */
	    sprintf(cmd, "%s > %s", COMPRESS_PATH, indexfilename);
	    if ((outfp = popen(cmd, "w")) == NULL)
	    {
		printf("Couldn't open pipe through '%s' to indexfile '%s'\n",
		       COMPRESS_PATH, indexfilename);
		continue;
	    }
	    outfd = fileno(outfp);

	    /* retreive the index file */
	    retrieve_index(outfd);

	    pclose(outfp);
	}
	dbprintf(("\n"));
    }
    
    dbclose();
    return 0;
}
