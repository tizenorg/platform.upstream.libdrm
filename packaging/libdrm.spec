Name:           libdrm
Version:        2.4.45
Release:        4
License:        MIT
Url:            http://cgit.freedesktop.org/mesa/drm
Summary:        Userspace interface to kernel DRM services
Group:          Graphics & UI Framework/Libraries
Source0:        %{name}-%{version}.tar.bz2
Source1001:     libdrm.manifest
BuildRequires:  kernel-headers
BuildRequires:  pkgconfig(pciaccess)
BuildRequires:  pkgconfig(pthread-stubs)
BuildRequires:  pkgconfig(xorg-macros)

%description
Direct Rendering Manager headers and kernel modules.

%package devel
Summary:        Userspace interface to kernel DRM services
Requires:       kernel-headers
Requires:       libdrm = %{version}
%ifnarch %{arm}
Requires:       libdrm-intel = %{version}
%endif
Requires:       libkms = %{version}

%description devel
Direct Rendering Manager headers and kernel modules.

Development related files.

%package -n libkms
Summary:        Userspace interface to kernel DRM buffer management

%description -n libkms
Userspace interface to kernel DRM buffer management

%package intel
Summary:        Userspace interface to intel graphics kernel DRM buffer management

%description intel
Userspace interface to intel graphics kernel DRM buffer management

%prep
%setup -q


%build
cp %{SOURCE1001} .
%reconfigure \
        --enable-static=yes  \
        --enable-udev \
        --enable-libkms \
        --disable-nouveau-experimental-api \
        --disable-radeon \
        --disable-nouveau \
        --enable-exynos-experimental-api

make %{?_smp_mflags}

%install
%make_install


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
