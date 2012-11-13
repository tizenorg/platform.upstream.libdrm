SUBDIRS = .

AM_CFLAGS = \
	$(WARN_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/slp \
	$(PTHREADSTUBS_CFLAGS) \
	-I$(top_srcdir)/include/drm

libdrm_slp_la_LTLIBRARIES = libdrm_slp.la
libdrm_slp_ladir = $(libdir)
libdrm_slp_la_LDFLAGS = -version-number 1:0:0 -no-undefined
libdrm_slp_la_LIBADD = ../libdrm.la @PTHREADSTUBS_LIBS@ @CLOCK_LIB@ -ldl

libdrm_slp_la_SOURCES = \
	drm_slp_bufmgr.c \
	drm_slp_bufmgr.h

libdrm_slpincludedir = ${includedir}/libdrm
libdrm_slpinclude_HEADERS = drm_slp_bufmgr.h

pkgconfig_DATA = libdrm_slp.pc
