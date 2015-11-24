if HAVE_SPRD
AM_CFLAGS = \
	-I$(top_srcdir)/include/drm \
	-I$(top_srcdir)/sprd/ \
	-I$(top_srcdir)
else
AM_CFLAGS = \
	-I$(top_srcdir)/include/drm \
	-I$(top_srcdir)
endif

if HAVE_INSTALL_TESTS
bin_PROGRAMS = \
	modeprint
else
noinst_PROGRAMS = \
	modeprint
endif

modeprint_SOURCES = \
	modeprint.c
if HAVE_SPRD
modeprint_LDADD = \
	$(top_builddir)/sprd/libdrm_sprd.la \
	$(top_builddir)/libdrm.la
else
modeprint_LDADD = \
	$(top_builddir)/libdrm.la
endif
