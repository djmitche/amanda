#include "amanda.h"
#include "version.h"

const int   VERSION_MAJOR   = 2;
const int   VERSION_MINOR   = 3;
const int   VERSION_PATCH   = 0;
const char *VERSION_COMMENT = ".4";

char *
versionsuffix()
{
#ifdef USE_VERSION_SUFFIXES
	static char vsuff[15];

	sprintf(vsuff,"-%d.%d.%d%s", 
		VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_COMMENT);
	return vsuff;
#else
	return "";
#endif
}

char *
version()
{
	static char vsuff[15];

	sprintf(vsuff,"%d.%d.%d%s", 
		VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_COMMENT);
	return vsuff;
}
