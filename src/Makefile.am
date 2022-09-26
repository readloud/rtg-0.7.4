## Makefile.am -- Process this file with automake to produce Makefile.in

CGI_LIB_DIR        = $(top_srcdir)/@CGI_LIB_DIR@
GD_LIB_DIR         = $(top_srcdir)/@GD_LIB_DIR@
PNG_LIB_DIR        = $(top_srcdir)/@PNG_LIB_DIR@
ZLIB_LIB_DIR       = $(top_srcdir)/@ZLIB_LIB_DIR@
INCLUDES           = -I$(CGI_LIB_DIR) -I$(GD_LIB_DIR) -I$(PNG_LIB_DIR) -I$(ZLIB_LIB_DIR)

RTG_LIBS = $(CGI_LIB_DIR)/libcgi.a $(GD_LIB_DIR)/libgd.a \
           $(PNG_LIB_DIR)/libpng.a $(ZLIB_LIB_DIR)/libzlib.a


rtgpoll_SOURCES = rtgsnmp.c rtgmysql.c rtgpoll.c rtgutil.c rtghash.c
rtgplot_SOURCES = rtgplot.c rtgmysql.c rtgutil.c 

include_HEADERS = rtg.h rtgplot.h common.h

rtgplot_LDADD = $(RTG_LIBS)

bin_PROGRAMS = rtgpoll rtgplot
