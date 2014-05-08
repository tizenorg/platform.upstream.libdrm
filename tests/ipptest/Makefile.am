AM_CFLAGS = \
	-I$(top_srcdir)/include/drm \
	-I$(top_srcdir)/libkms/ \
	-I$(top_srcdir)/exynos/ \
	-I$(top_srcdir)

noinst_PROGRAMS = \
	ipptest

ipptest_SOURCES = \
	fimctest.c \
	gem.c \
	util.c \
	fimc.c

ipptest_LDADD = \
	$(top_builddir)/libdrm.la \
	$(top_builddir)/libkms/libkms.la \
	$(top_builddir)/exynos/libdrm_exynos.la
