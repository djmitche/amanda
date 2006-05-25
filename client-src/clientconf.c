/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-2000 University of Maryland at College Park
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
 * $Id: clientconf.c,v 1.3 2006/05/25 01:47:11 johnfranks Exp $
 *
 * read configuration file
 */
/*
 *
 * XXX - I'm not happy *at all* with this implementation, but I don't
 * think YACC would be any easier.  A more table based implementation
 * would be better.  Also clean up memory leaks.
 */
#include "amanda.h"
#include "arglist.h"
#include "util.h"
#include "clientconf.h"
#include "clock.h"

/* configuration parameters */
static char *cln_config_dir = NULL;

/* strings */
static val_t cln_conf;
static val_t cln_index_server;
static val_t cln_tape_server;
static val_t cln_tapedev;
static val_t cln_auth;
static val_t cln_ssh_keys;

/* ints */

/* reals */

/* other internal variables */

static int seen_conf;
static int seen_index_server;
static int seen_tape_server;
static int seen_tapedev;
static int seen_auth;
static int seen_ssh_keys;

/* predeclare local functions */

static void init_defaults(void);
static void read_conffile_recursively(char *filename);

static int read_confline(void);

/*
** ------------------------
**  External entry points
** ------------------------
*/

int
read_clientconf(
    char *	filename)
{
    init_defaults();

    /* We assume that conf_confname & conf_conf are initialized to NULL above */
    read_conffile_recursively(filename);

    return got_parserror;
}

struct byname {
    char *name;
    cconfparm_t parm;
    tok_t typ;
} byname_table [] = {
    { "CONF",		CLN_CONF,		CONF_STRING },
    { "INDEX_SERVER",	CLN_INDEX_SERVER,	CONF_STRING },
    { "TAPE_SERVER",	CLN_TAPE_SERVER,	CONF_STRING },
    { "TAPEDEV",	CLN_TAPEDEV,		CONF_STRING },
    { "AUTH",		CLN_AUTH,		CONF_STRING },
    { "SSH_KEYS",	CLN_SSH_KEYS,		CONF_STRING },
    { NULL,		CLN_CONF,		CONF_UNKNOWN }
};

char *
client_getconf_byname(
    char *	str)
{
    static char *tmpstr;
    char number[NUM_STR_SIZE];
    struct byname *np;
    char *s;
    char ch;

    tmpstr = stralloc(str);
    s = tmpstr;
    while((ch = *s++) != '\0') {
	if(islower((int)ch))
	    s[-1] = (char)toupper(ch);
    }

    for(np = byname_table; np->name != NULL; np++)
	if(strcmp(np->name, tmpstr) == 0) break;

    if(np->name == NULL) return NULL;

    if(np->typ == CONF_INT) {
	snprintf(number, sizeof(number), "%d", client_getconf_int(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else if(np->typ == CONF_BOOL) {
	if(client_getconf_int(np->parm) == 0) {
	    tmpstr = newstralloc(tmpstr, "off");
	}
	else {
	    tmpstr = newstralloc(tmpstr, "on");
	}
    } else if(np->typ == CONF_REAL) {
	snprintf(number, sizeof(number), "%lf", client_getconf_real(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else {
	tmpstr = newstralloc(tmpstr, client_getconf_str(np->parm));
    }

    return tmpstr;
}

int
client_getconf_seen(
    cconfparm_t parm)
{
    switch(parm) {
    case CLN_CONF: return seen_conf;
    case CLN_INDEX_SERVER: return seen_index_server;
    case CLN_TAPE_SERVER: return seen_tape_server;
    case CLN_TAPEDEV: return seen_tapedev;
    case CLN_AUTH: return seen_auth;
    case CLN_SSH_KEYS: return seen_ssh_keys;
    default: return 0;
    }
}

int
client_getconf_int(
    cconfparm_t	parm)
{
    int r = 0;

    switch(parm) {
    default:
	error("error [unknown client_getconf_int parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

am64_t
client_getconf_am64(
    cconfparm_t	parm)
{
    am64_t r = 0;

    switch(parm) {
    default:
	error("error [unknown client_getconf_am64 parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

double
client_getconf_real(
    cconfparm_t	parm)
{
    double r = 0;

    switch(parm) {
    default:
	error("error [unknown client_getconf_real parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

char *
client_getconf_str(
    cconfparm_t parm)
{
    char *r = 0;

    switch(parm) {

    case CLN_CONF: r = cln_conf.s; break;
    case CLN_INDEX_SERVER: r = cln_index_server.s; break;
    case CLN_TAPE_SERVER: r = cln_tape_server.s; break;
    case CLN_TAPEDEV: r = cln_tapedev.s; break;
    case CLN_AUTH: r = cln_auth.s; break;
    case CLN_SSH_KEYS: r = cln_ssh_keys.s; break;

    default:
	error("error [unknown client_getconf_str parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

/*
** ------------------------
**  Internal routines
** ------------------------
*/


static void
init_defaults(void)
{
    char *s;

    /* defaults for exported variables */

#ifdef DEFAULT_CONFIG
    s = DEFAULT_CONFIG;
#else
    s = "";
#endif
    cln_conf.s = stralloc(s);

#ifdef DEFAULT_SERVER
    s = DEFAULT_SERVER;
#else
    s = "";
#endif
    cln_index_server.s = stralloc(s);


#ifdef DEFAULT_TAPE_SERVER
    s = DEFAULT_TAPE_SERVER;
#else
#ifdef DEFAULT_SERVER
    s = DEFAULT_SERVER;
#else
    s = "";
#endif
#endif
    cln_tape_server.s = stralloc(s);

#ifdef DEFAULT_TAPE_DEVICE
    s = DEFAULT_TAPE_DEVICE;
#else
    s = "/dev/nst0";
#endif
    cln_tapedev.s = stralloc(s);

    cln_auth.s = stralloc("bsd");

    cln_ssh_keys.s = stralloc("");

    /* defaults for internal variables */

    seen_conf = 0;
    seen_index_server = 0;
    seen_tape_server = 0;
    seen_tapedev = 0;
    seen_auth = 0;
    seen_ssh_keys = 0;

    conf_line_num = got_parserror = 0;
    allow_overwrites = 0;
    token_pushed = 0;

}

static void
read_conffile_recursively(
    char *	filename)
{
    /* Save globals used in read_confline(), elsewhere. */
    int  save_line_num  = conf_line_num;
    FILE *save_conf     = conf_conf;
    char *save_confname = conf_confname;
    int	rc;

    if (*filename == '/' || cln_config_dir == NULL) {
	conf_confname = stralloc(filename);
    } else {
	conf_confname = stralloc2(cln_config_dir, filename);
    }

    if((conf_conf = fopen(conf_confname, "r")) == NULL) {
	fprintf(stderr, "could not open conf file \"%s\": %s\n", conf_confname,
		strerror(errno));
	amfree(conf_confname);
	got_parserror = -1;
	return;
    }

    conf_line_num = 0;

    /* read_confline() can invoke us recursively via "includefile" */
    do {
	rc = read_confline();
    } while (rc != 0);
    afclose(conf_conf);

    amfree(conf_confname);

    /* Restore globals */
    conf_line_num = save_line_num;
    conf_conf     = save_conf;
    conf_confname = save_confname;
}


/* ------------------------ */


keytab_t main_keytable[] = {
    { "CONF", CONF_CONF },
    { "INDEX_SERVER", CONF_INDEX_SERVER },
    { "TAPE_SERVER", CONF_TAPE_SERVER },
    { "TAPEDEV", CONF_TAPEDEV },
    { "AUTH", CONF_AUTH },
    { "SSH_KEYS", CONF_SSH_KEYS },
    { NULL, CONF_IDENT }
};

static int
read_confline(void)
{
    keytable = main_keytable;

    conf_line_num += 1;
    get_conftoken(CONF_ANY);
    switch(tok) {
    case CONF_INCLUDEFILE:
	{
	    char *fn;

	    get_conftoken(CONF_STRING);
	    fn = tokenval.s;
	    read_conffile_recursively(fn);
	}
	break;

    case CONF_CONF:
	get_simple(&cln_conf, &seen_conf, CONF_STRING);
	break;

    case CONF_INDEX_SERVER:
	get_simple(&cln_index_server, &seen_index_server, CONF_STRING);
	break;

    case CONF_TAPE_SERVER:
	get_simple(&cln_tape_server, &seen_tape_server, CONF_STRING);
	break;

    case CONF_TAPEDEV:
	get_simple(&cln_tapedev, &seen_tapedev, CONF_STRING);
	break;

    case CONF_AUTH:
	get_simple(&cln_auth, &seen_auth, CONF_STRING);
	break;

    case CONF_SSH_KEYS:
	get_simple(&cln_ssh_keys, &seen_ssh_keys, CONF_STRING);
	break;

    case CONF_NL:	/* empty line */
	break;

    case CONF_END:	/* end of file */
	return 0;

    default:
	conf_parserror("configuration keyword expected");
    }
    if(tok != CONF_NL)
	get_conftoken(CONF_NL);
    return 1;
}



/* ------------------------ */

#ifdef TEST

static char *cln_config_name = NULL;
static char *cln_config_dir = NULL;

void
dump_configuration(
    char *filename)
{
    printf("AMANDA CLIENT CONFIGURATION FROM FILE \"%s\":\n\n", filename);

    printf("cln_conf = \"%s\"\n", client_getconf_str(CLN_CONF));
    printf("cln_index_server = \"%s\"\n", client_getconf_str(CLN_INDEX_SERVER));
    printf("cln_tape_server = \"%s\"\n", client_getconf_str(CLN_TAPE_SERVER));
    printf("cln_tapedev = \"%s\"\n", client_getconf_str(CLN_TAPEDEV));
    printf("cln_auth = \"%s\"\n", client_getconf_str(CLN_AUTH));
    printf("cln_ssh_keys = \"%s\"\n", client_getconf_str(CLN_SSH_KEYS));
}

int
main(
    int		argc,
    char **	argv)
{
  char *conffile;
  char *diskfile;
  disklist_t lst;
  int result;
  unsigned long malloc_hist_1, malloc_size_1;
  unsigned long malloc_hist_2, malloc_size_2;

  safe_fd(-1, 0);

  set_pname("conffile");

  /* Don't die when child closes pipe */
  signal(SIGPIPE, SIG_IGN);

  malloc_size_1 = malloc_inuse(&malloc_hist_1);

  startclock();

  if (argc > 1) {
    if (argv[1][0] == '/') {
      cln_config_dir = stralloc(argv[1]);
      cln_config_name = strrchr(cln_config_dir, '/') + 1;
      cln_config_name[-1] = '\0';
      cln_config_dir = newstralloc2(cln_config_dir, cln_config_dir, "/");
    } else {
      cln_config_name = stralloc(argv[1]);
      cln_config_dir = vstralloc(CONFIG_DIR, "/", cln_config_name, "/", NULL);
    }
  } else {
    char my_cwd[STR_SIZE];

    if (getcwd(my_cwd, sizeof(my_cwd)) == NULL) {
      error("cannot determine current working directory");
    }
    cln_config_dir = stralloc2(my_cwd, "/");
    if ((cln_config_name = strrchr(my_cwd, '/')) != NULL) {
      cln_config_name = stralloc(cln_config_name + 1);
    }
  }

  conffile = stralloc2(cln_config_dir, CONFFILE_NAME);
  result = read_conffile(conffile);
  if (result == 0) {
      diskfile = client_getconf_str(CNF_DISKFILE);
      if (diskfile != NULL && access(diskfile, R_OK) == 0) {
	  result = read_diskfile(diskfile, &lst);
      }
  }
  dump_configuration(CONFFILE_NAME);
  amfree(conffile);

  malloc_size_2 = malloc_inuse(&malloc_hist_2);

  if(malloc_size_1 != malloc_size_2) {
    malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
  }

  return result;
}

#endif /* TEST */

char *
generic_client_get_security_conf(
    char *	string,
    void *	arg)
{
	(void)arg;	/* Quiet unused parameter warning */

	if(!string || !*string)
		return(NULL);

	if(strcmp(string, "conf")==0) {
		return(client_getconf_str(CLN_CONF));
	} else if(strcmp(string, "index_server")==0) {
		return(client_getconf_str(CLN_INDEX_SERVER));
	} else if(strcmp(string, "tape_server")==0) {
		return(client_getconf_str(CLN_TAPE_SERVER));
	} else if(strcmp(string, "tapedev")==0) {
		return(client_getconf_str(CLN_TAPEDEV));
	} else if(strcmp(string, "auth")==0) {
		return(client_getconf_str(CLN_AUTH));
	} else if(strcmp(string, "ssh_keys")==0) {
		return(client_getconf_str(CLN_SSH_KEYS));
/*
	} else if(strcmp(string, "krb5principal")==0) {
		return(client_getconf_str(CNF_KRB5PRINCIPAL));
	} else if(strcmp(string, "krb5keytab")==0) {
		return(client_getconf_str(CNF_KRB5KEYTAB));
*/
	}
	return(NULL);
}
