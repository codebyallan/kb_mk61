Name:           mk61
Version:        0.5.0
Release:        1%{?dist}
Summary:        Clean-room keyboard stack for Linux (library, daemon and CLI)
License:        GPL-2.0-only
URL:            https://github.com/codebyallan/kb_mk61
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  systemd-rpm-macros

%package devel
Summary:        Development files for libkbcore
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description
Clean-room reimplementation of a keyboard configuration stack for Linux:
- libkbcore: protocol, profiles and macros library (no GUI, no global I/O)
- kbdd: user daemon (hidraw, optional uinput, Unix-socket IPC)
- kbctl: command-line client for kbdd
Status: experimental; device writes are disabled until validated on hardware.

%description devel
Header, static library and development symlink for building against libkbcore.

%prep
%setup -q

%build
%make_build all

%install
%make_install PREFIX=%{_prefix} LIBDIR=%{_libdir}

%post
%systemd_user_post kbdd.service
# Do not enable or start the user service for all logged-in users.
# Installation must not activate input notifications without an explicit
# per-user opt-in. A user who wants the daemon should run:
#   systemctl --user enable --now kbdd.service
systemctl --global disable kbdd.service >/dev/null 2>&1 || :

%preun
%systemd_user_preun kbdd.service

%postun
%systemd_user_postun_with_restart kbdd.service
if [ "$1" -eq 0 ]; then
    systemctl --global disable kbdd.service >/dev/null 2>&1 || :
fi

%files
%{_bindir}/kbdd
%{_bindir}/kbctl
%{_libdir}/libkbcore.so.*
%{_prefix}/lib/udev/rules.d/60-kbdd.rules
%{_prefix}/lib/systemd/user/kbdd.service

%files devel
%{_includedir}/kbcore.h
%{_libdir}/libkbcore.a
%{_libdir}/libkbcore.so

%changelog
* Mon Nov 02 2026 mk61 maintainers - 0.4.1-1
- remove public generic profile write
- add verified profile recovery
- standardize profile numbering to hardware range 1-4

* Mon Nov 02 2026 mk61 maintainers - 0.4.0-1
- Add report-rate and animation-direction controls.

* Sun Nov 01 2026 mk61 maintainers - 0.3.3-1
- Add safe profile LED block persistence through kbctl.

* Sun Nov 01 2026 mk61 maintainers - 0.3.2-1
- Bump version for the latest fixes.

* Sun Nov 01 2026 mk61 maintainers - 0.3.1-1
- Fix user service startup and capability setup on restricted systemd user managers

* Sun Nov 01 2026 mk61 maintainers - 0.3.0-1
- Add per-key LED color commands and MK61L/SK61L key mapping

* Fri Oct 30 2026 mk61 maintainers - 0.1.0-1
- Initial package (CLI/daemon only, no GUI)
