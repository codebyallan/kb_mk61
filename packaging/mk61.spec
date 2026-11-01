Name:           mk61
Version:        0.3.0
Release:        1%{?dist}
Summary:        Clean-room keyboard stack for Linux (library, daemon and CLI)
License:        GPL-2.0-only
URL:            https://github.com/codebyallan/kb_mk61
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  systemd-rpm-macros

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
install -Dpm0644 /dev/stdin %{buildroot}%{_sysconfdir}/systemd/user/kbdd.service.d/led.conf <<'EOF'
[Service]
Environment=KBDD_ENABLE_LED=1
Environment=KBDD_ENABLE_PROFILE_WRITE=1
EOF

%post
%systemd_user_post kbdd.service
systemctl --global enable kbdd.service >/dev/null 2>&1 || :
loginctl list-users --no-legend 2>/dev/null | while read -r uid _; do
    [ -n "$uid" ] || continue
    systemctl --user --machine="${uid}@.host" daemon-reload >/dev/null 2>&1 || :
    systemctl --user --machine="${uid}@.host" start kbdd.service >/dev/null 2>&1 || :
done

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
%{_libdir}/libkbcore.a
%{_libdir}/libkbcore.so
%{_includedir}/kbcore.h
%{_prefix}/lib/udev/rules.d/60-kbdd.rules
%{_prefix}/lib/systemd/user/kbdd.service
%config(noreplace) %{_sysconfdir}/systemd/user/kbdd.service.d/led.conf

%changelog
* Sun Nov 01 2026 mk61 maintainers - 0.3.0-1
- Add per-key LED color commands and MK61L/SK61L key mapping

* Fri Oct 30 2026 mk61 maintainers - 0.1.0-1
- Initial package (CLI/daemon only, no GUI)
