/*
  indexfilename - generates a name for the temporary file in which an index
  file is stored on the client
*/

#include "amanda.h"
#include "indexfilename.h"

char *indexfilename(host, disk, level, datestamp)
const char *host;
const char *disk;
int level;
const char *datestamp;
{
    int i;
    static char name[1024];

    sprintf(name, "%s/%s_%s_%c%c%c%c-%c%c-%c%c_%d%s",
	    INDEX_TMP_DIR, host, disk,
	    datestamp[0], datestamp[1], datestamp[2], datestamp[3], 
	    datestamp[4], datestamp[5], datestamp[6], datestamp[7], 
	    level, COMPRESS_SUFFIX);

    for (i = strlen(INDEX_TMP_DIR)+1; i < strlen(name); i++)
	if ((name[i] == '/') || (name[i] == ' '))
	    name[i] = '_';
    
    return name;
}
