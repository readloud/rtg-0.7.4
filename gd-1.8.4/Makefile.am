## Process this file with automake to produce Makefile.in

ZLIB_LIB_DIR = @ZLIB_LIB_DIR@
PNG_LIB_DIR  = @PNG_LIB_DIR@

INCLUDES = -I$(top_srcdir)/$(ZLIB_LIB_DIR) -I$(top_srcdir)/$(PNG_LIB_DIR)

noinst_LIBRARIES  = libgd.a

libgd_a_SOURCES = \
	gd.c		gd_io.c     	gd_io_dp.c	gd_io_file.c \
	gd_png.c	gdfontg.c	gdfontl.c	gdfontmb.c   \
	gdfontmb.c	gdfonts.c	gdhelpers.c 	gdtables.c   \
	gd.h		gd_io.h		gdcache.h	gdfontg.h \
	gdfontl.h	gdfontmb.h	gdfonts.h	gdfontt.h \
	gdhelpers.h 

libgd_a_CFLAGS = -DHAVE_LIBPNG

EXTRA_DIST = \
	index.html 	readme.txt 	gd2copypal.c 	gd2time.c \
	gd2topng.c 	gdcache.c	gddemo.c	gdkanji.c \
	gdparttopng.c	gdtest.c	gdtestft.c	gdtestttf.c\
	gdtopng.c	mathmake.c	pngtogd.c	pngtogd2.c \
	gdft.c		gd_ss.c		gd_io_ss.c	gd_gd.c	   \
	gd_gd2.c	gd_jpeg.c	wbmp.c		webpng.c
