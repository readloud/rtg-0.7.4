/****************************************************************************
   Program:     $Id: rtgmysql.c,v 1.10 2003/05/21 20:20:38 rbeverly Exp $
   Author:      $Author: rbeverly $
   Date:        $Date: 2003/05/21 20:20:38 $
   Purpose:     RTG MySQL routines
****************************************************************************/

#include "common.h"
#include "rtg.h"

extern FILE *dfp;

int db_insert(char *query, MYSQL * mysql)
{
    if (set.verbose >= HIGH)
	printf("SQL: %s\n", query);
    if (mysql_query(mysql, query)) {
	if (set.verbose >= LOW)
	    fprintf(stderr, "** MySQL Error: %s\n", mysql_error(mysql));
	return (FALSE);
    } else
	return (TRUE);
}


int rtg_dbconnect(char *database, MYSQL * mysql)
{
    if (set.verbose >= LOW)
	fprintf(dfp, "Connecting to MySQL database '%s' on '%s'...", database, set.dbhost);
    mysql_init(mysql);
    if (!mysql_real_connect
     (mysql, set.dbhost, set.dbuser, set.dbpass, database, 0, NULL, 0)) {
	fprintf(dfp, "** Failed: %s\n", mysql_error(mysql));
	return (-1);
    } else
	return (0);
}


void rtg_dbdisconnect(MYSQL * mysql)
{
    mysql_close(mysql);
}
