%{
#include "amanda.h"
#include "amrecover.h"
    
void yyerror P((char *s));
extern int yylex P((void));
%}

/* DECLARATIONS */
%union {
  int intval;
  double floatval;
  char *strval;
  int subtok;
}

	/* literal keyword tokens */

%token SETHOST SETDISK SETDATE SETMODE CD QUIT DHIST LS ADD EXTRACT
%token LIST DELETE PWD CLEAR HELP LCD LPWD

        /* typed tokens */

%token <strval> PATH
%token <strval> DATE

/* GRAMMAR */
%%

ucommand:
	set_command
  |     display_command
  |     quit_command
  |     add_command
  |     delete_command
  |     local_command
  |	help_command
  |     extract_command
  ;

set_command:
	SETDATE DATE { set_date($2); }
  |     SETHOST PATH { set_host($2); }
  |     SETDISK PATH PATH { set_disk($2, $3); }
  |     SETDISK PATH { set_disk($2, NULL); }
  |     CD PATH { set_directory($2); }
  ;

display_command:
	DHIST { list_disk_history(); }
  |     LS { list_directory(); }
  |     LIST PATH { display_extract_list($2); }
  |     LIST { display_extract_list(NULL); }
  |     PWD { show_directory(); }
  |     CLEAR { clear_extract_list(); }    
  ;

quit_command:
	QUIT { quit(); }
  ;

add_command:
	ADD add_path
  ;

add_path:
	add_path PATH { add_file($2); }
  |     PATH { add_file($1); }
  ;

delete_command:
	DELETE delete_path
  ;

delete_path:
	delete_path PATH { delete_file($2); }
  |     PATH { delete_file($1); }
  ;

local_command:
	LPWD { char buf[1024]; printf("%s\n", getcwd(buf, 1024)); }
  |     LCD PATH { if (chdir($2) == -1) printf("%s: No such directory\n", $2); }

help_command:
	HELP { help_list(); }
  ;

extract_command:
	EXTRACT { extract_files(); }

/* ADDITIONAL C CODE */
%%

void yyerror(s)
char *s;
{
  printf("Invalid command - %s\n", s);
}

