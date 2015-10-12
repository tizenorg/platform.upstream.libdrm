AM_CFLAGS = \
	$(WARN_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/sprd \
	$(PTHREADSTUBS_CFLAGS) \
	-I$(top_srcdir)/include/drm

libdrm_sprd_la_LTLIBRARIES = libdrm_sprd.la
libdrm_sprd_ladir = $(libdir)
libdrm_sprd_la_LDFLAGS = -version-number 1:0:0 -no-undefined
libdrm_sprd_la_LIBADD = ../libdrm.la @PTHREADSTUBS_LIBS@

libdrm_sprd_la_SOURCES = sprd_drm.c

libdrm_sprdcommonincludedir = ${includedir}/sprd
libdrm_sprdcommoninclude_HEADERS = sprd_drm.h

libdrm_sprdincludedir = ${includedir}/libdrm
libdrm_sprdinclude_HEADERS = sprd_drmif.h

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = libdrm_sprd.pc
