Name:           libdrm
Version:        2.4.42
Release:        4
License:        MIT
Url:            http://cgit.freedesktop.org/mesa/drm
Summary:        Userspace interface to kernel DRM services
Group:          Graphics/Libraries
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
Group:          Development/Libraries
Requires:       kernel-headers
Requires:       libdrm
%ifnarch %{arm}
Requires:       libdrm-intel
%endif
%if 0%{?enable_slp}
Requires:       libdrm-slp
%endif
Requires:       libkms

%description devel
Direct Rendering Manager headers and kernel modules.

Development related files.

%if 0%{?enable_slp}
%package slp
Summary:        Userspace interface to slp-specific kernel DRM services
Group:          Development/Libraries

%description slp
Userspace interface to slp-specific kernel DRM services.
%endif

%package -n libkms
Summary:        Userspace interface to kernel DRM buffer management
Group:          Development/Libraries

%description -n libkms
Userspace interface to kernel DRM buffer management

%package intel
Summary:        Userspace interface to intel graphics kernel DRM buffer management
Group:          Development/Libraries

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

%if 0%{?enable_slp}
%post slp -p /sbin/ldconfig

%postun slp  -p /sbin/ldconfig
%endif

%post -n libkms -p /sbin/ldconfig

%postun -n libkms -p /sbin/ldconfig

%post intel -p /sbin/ldconfig

%postun intel -p /sbin/ldconfig

%files
%manifest libdrm.manifest
%{_libdir}/libdrm.so.*
%{_libdir}/libdrm_exynos.so.*

%files devel
%manifest libdrm.manifest
%{_includedir}/*
%{_libdir}/libdrm.so
%if 0%{?enable_slp}
%{_libdir}/libdrm_slp.so
%endif
%ifarch i586 i686 %ix86 x86_64
%{_libdir}/libdrm_intel.so
%endif
%{_libdir}/libkms.so
%{_libdir}/libdrm_exynos.so
%{_libdir}/pkgconfig/*


%if 0%{?enable_slp}
%files slp
%manifest libdrm.manifest
%{_libdir}/libdrm_slp*.so.*
%endif

%files -n libkms
%manifest libdrm.manifest
%{_libdir}/libkms.so.*

%ifarch i586 i686 %ix86 x86_64
%files intel
%manifest libdrm.manifest
%{_libdir}/libdrm_intel.so.*
%endif
