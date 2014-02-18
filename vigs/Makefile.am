#
# Update "-version-number M:m:r" whenever library interface changes.
# Only "M" part has to be updated.
#

AM_CFLAGS = \
	$(WARN_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/vigs \
	$(PTHREADSTUBS_CFLAGS) \
	-I$(top_srcdir)/include/drm

libdrm_vigs_la_LTLIBRARIES = libdrm_vigs.la
libdrm_vigs_ladir = $(libdir)
libdrm_vigs_la_LDFLAGS = -version-number 7:0:0 -no-undefined
libdrm_vigs_la_LIBADD = ../libdrm.la @PTHREADSTUBS_LIBS@

libdrm_vigs_la_SOURCES = vigs.c

libdrm_vigsincludedir = ${includedir}/libdrm
libdrm_vigsinclude_HEADERS = vigs.h

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = libdrm_vigs.pc
