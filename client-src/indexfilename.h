/*
  indexfilename - generates a name for the temporary file in which an index
  file is stored on the client
*/

extern char *indexfilename P((const char *host,
			      const char *disk,
			      int level,
			      const char *datestamp));
