Name:           mk61
Version:        0.1.0
Release:        1%{?dist}
Summary:        Clean-room keyboard stack for Linux (library, daemon and CLI)
License:        GPL-2.0-only
URL:            https://github.com/example/mk61
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make

%description
Clean-room reimplementation of a keyboard configuration stack for Linux:
- libkbcore: protocol, profiles and macros library (no GUI, no global I/O)
- kbdd: user daemon (hidraw, optional uinput, Unix-socket IPC)
- kbctl: command-line client for kbdd
Status: experimental; device writes are disabled until validated on hardware.

%prep
%setup -q

%build
%make_build all

%install
%make_install PREFIX=%{_prefix} LIBDIR=%{_libdir}

%files
%{_bindir}/kbdd
%{_bindir}/kbctl
%{_libdir}/libkbcore.a
%{_libdir}/libkbcore.so
%{_includedir}/kbcore.h
%{_prefix}/lib/udev/rules.d/60-kbdd.rules
%{_prefix}/lib/systemd/user/kbdd.service

%changelog
* Fri Oct 30 2026 mk61 maintainers - 0.1.0-1
- Initial package (CLI/daemon only, no GUI)
