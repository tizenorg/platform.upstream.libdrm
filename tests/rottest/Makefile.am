AM_CFLAGS = \
	-I$(top_srcdir)/include/drm \
	-I$(top_srcdir)/libkms/ \
	-I$(top_srcdir)/exynos/ \
	-I$(top_srcdir)

if HAVE_INSTALL_TESTS
bin_PROGRAMS = \
	rottest
else
noinst_PROGRAMS = \
	rottest
endif

rottest_SOURCES = \
	rottest.c \
	gem.c \
	util.c \
	rotator.c

rottest_LDADD = \
	$(top_builddir)/libdrm.la \
	$(top_builddir)/libkms/libkms.la \
	$(top_builddir)/exynos/libdrm_exynos.la
