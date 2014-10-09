Name:           libdrm
Version:        2.4.52
Release:        0
License:        MIT
Url:            http://cgit.freedesktop.org/mesa/drm
Summary:        Userspace interface to kernel DRM services
Group:          Graphics & UI Framework/Libraries
Source0:        %{name}-%{version}.tar.bz2
Source1001:     libdrm.manifest

BuildRequires:  kernel-headers
BuildRequires:  pkgconfig(pciaccess)
BuildRequires:  pkgconfig(pthread-stubs)

%description
Direct Rendering Manager headers and kernel modules.

%package tools
Summary:        Diagnostic utilities for DRI and DRM
Group:          Graphics & UI Framework/Utilities
Obsoletes:      libdrm < %version-%release
Provides:       libdrm = %version-%release

%description tools
Diagnoistic tools to run a test for DRI and DRM

%package tools-exynos
Summary:	Diagnostic utilities for exynos
Group:          Graphics & UI Framework/Utilities

%description tools-exynos
Diagnoistic tools to run a test for exynos

%package devel
Summary:        Userspace interface to kernel DRM services
Requires:       kernel-headers
Requires:       libdrm = %{version}
%ifarch i586 i686 %ix86 x86_64
Requires:       libdrm-intel = %{version}
%endif
Requires:       libkms = %{version}

%description devel
Direct Rendering Manager headers and kernel modules.

Development related files.

%package -n libkms
Summary:        Userspace interface to kernel DRM buffer management

%description -n libkms
Userspace interface to kernel DRM buffer management files

%package intel
Summary:        Userspace interface to intel graphics kernel DRM buffer management

%description intel
Userspace interface to intel graphics kernel DRM buffer management files

%prep
%setup -q
cp %{SOURCE1001} .

%build
%reconfigure \
        --enable-static=yes  \
        --enable-udev \
        --enable-libkms \
        --disable-nouveau-experimental-api \
        --disable-radeon \
        --disable-nouveau \
        --enable-exynos-experimental-api \
        --enable-install-test-programs \
        --disable-cairo-tests

%__make %{?_smp_mflags}
%__make %{?_smp_mflags} -C tests dristat drmstat

%install
%make_install
%{__mkdir} -p %{buildroot}%{_bindir}
%{__install}  \
        tests/.libs/dristat \
        tests/.libs/drmstat \
        %{buildroot}%{_bindir}

rm -f %{buildroot}%{_bindir}/kmstest

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%post -n libkms -p /sbin/ldconfig

%postun -n libkms -p /sbin/ldconfig

%post intel -p /sbin/ldconfig

%postun intel -p /sbin/ldconfig

%files
%manifest %{name}.manifest
%{_libdir}/libdrm.so.*
%{_libdir}/libdrm_exynos.so.*
%{_libdir}/libdrm_vigs.so.*

%files tools
%manifest %{name}.manifest
%{_bindir}/dristat
%{_bindir}/drmstat
%{_bindir}/modeprint
%{_bindir}/modetest

%files tools-exynos
%manifest %{name}.manifest
%_bindir/exynos_fimg2d_test
%_bindir/ipptest
%_bindir/rottest

%files devel
%manifest %{name}.manifest
%dir %{_includedir}/libdrm
%{_includedir}/libdrm/*.h
%dir %{_includedir}/libkms
%{_includedir}/libkms/*.h
%dir %{_includedir}/exynos
%{_includedir}/exynos/*.h
%{_includedir}/*.h
%{_libdir}/libdrm.so
%ifarch i586 i686 %ix86 x86_64
%{_libdir}/libdrm_intel.so
%endif
%{_libdir}/libkms.so
%{_libdir}/libdrm_exynos.so
%{_libdir}/libdrm_vigs.so
%{_libdir}/pkgconfig/*

%files -n libkms
%manifest %{name}.manifest
%{_libdir}/libkms.so.*

%ifarch i586 i686 %ix86 x86_64
%files intel
%manifest %{name}.manifest
%{_libdir}/libdrm_intel.so.*
%endif
