%define name uAuditAnalyzer3
%define version 3.0.0
%define build_timestamp %{lua: print(os.date("%Y%m%d"))}
Name:           %{name}
Version:        %{version}
Release:        %{build_timestamp}.git%{?dist}
Summary:        Unmanarc's Auditd Analyzer
License:        GPLv3
URL:            https://github.com/unmanarc/uAuditAnalyzer3
Source0:        https://github.com/unmanarc/uAuditAnalyzer3/archive/master.tar.gz#/%{name}-%{version}-%{build_timestamp}.tar.gz
Group:          Applications/Internet
%define cmake cmake
%if 0%{?rhel} == 6
%define cmake cmake3
%endif
%if 0%{?rhel} == 7
%define cmake cmake3
%endif
%if 0%{?rhel} == 8
%define debug_package %{nil}
%endif
%if 0%{?rhel} == 9
%define debug_package %{nil}
%endif
%if 0%{?fedora} >= 33
%define debug_package %{nil}
%endif
BuildRequires: libMantids30-devel >= 3.0.0
BuildRequires: %{cmake} systemd zlib-devel boost-devel gcc-c++ sqlite-devel
%if 0%{?rhel} == 7
BuildRequires:  openssl11-devel
%else
BuildRequires:  openssl-devel
%endif
Requires: libMantids30 >= 3.0.0
Requires: zlib boost-regex boost-system boost-json sqlite
%if 0%{?rhel} == 7
Requires:       openssl11
%else
Requires:       openssl
%endif
%description
This package contains uAuditAnalyzer3 is intended to be a highly-efficient application for processing auditd logs received via rsyslog. It is being actively used in many enterprise-level environments for detecting potential threats and providing instant alerts using messaging applications, bots or other means.
%prep
%autosetup -n %{name}-master
%build
%if 0%{?rhel} == 7
%{cmake} -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON -DCMAKE_INSTALL_PREFIX:PATH=/usr -DCMAKE_BUILD_TYPE=MinSizeRel -DSSLRHEL7=ON
%else
%{cmake} -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON -DCMAKE_INSTALL_PREFIX:PATH=/usr -DCMAKE_BUILD_TYPE=MinSizeRel
%endif
make %{?_smp_mflags}
%clean
rm -rf $RPM_BUILD_ROOT
%install
rm -rf $RPM_BUILD_ROOT
%if 0%{?fedora} >= 33
ln -s . %{_host}
%endif
%if 0%{?rhel} >= 9
ln -s . %{_host}
ln -s . redhat-linux-build
%endif
%if 0%{?fedora} == 35
ln -s . redhat-linux-build
%endif
%if "%{_host}" == "powerpc64le-redhat-linux-gnu"
ln -s . ppc64le-redhat-linux-gnu
%endif
%if "%{_host}" == "s390x-ibm-linux-gnu"
ln -s . s390x-redhat-linux-gnu
%endif
%if "%{cmake}" == "cmake3"
%cmake3_install
%else
%cmake_install
%endif
mkdir -vp $RPM_BUILD_ROOT/var/www
cp -a uanlz_web/var/www/uauditweb3 $RPM_BUILD_ROOT/var/www
mkdir -vp $RPM_BUILD_ROOT/etc/uauditanalyzer3
cp -a uanlz_web/etc/uauditanalyzer3/uanlz_web $RPM_BUILD_ROOT/etc/uauditanalyzer3/
cp -a uanlz_log2json/etc/uauditanalyzer3/uanlz_log2json $RPM_BUILD_ROOT/etc/uauditanalyzer3/
cp -a uanlz_alert/etc/uauditanalyzer3/uanlz_alert $RPM_BUILD_ROOT/etc/uauditanalyzer3/
chmod 600 $RPM_BUILD_ROOT/etc/uauditanalyzer3/uanlz_web/snakeoil.key
chmod 600 $RPM_BUILD_ROOT/etc/uauditanalyzer3/uanlz_alert/*.json
chmod 600 $RPM_BUILD_ROOT/etc/uauditanalyzer3/*/*.ini
chmod 600 $RPM_BUILD_ROOT/etc/uauditanalyzer3/uanlz_web/webserver.conf
mkdir -vp $RPM_BUILD_ROOT/usr/lib/systemd/system
cp uanlz_web/etc/systemd/system/uanlz_web.service $RPM_BUILD_ROOT/usr/lib/systemd/system/
cp uanlz_log2json/etc/systemd/system/uanlz_log2json.service $RPM_BUILD_ROOT/usr/lib/systemd/system/
cp uanlz_alert/etc/systemd/system/uanlz_alert.service $RPM_BUILD_ROOT/usr/lib/systemd/system/
chmod 644 $RPM_BUILD_ROOT/usr/lib/systemd/system/*.service
%files
%doc
%{_bindir}/uanlz_alert
%{_bindir}/uanlz_log2json
%{_bindir}/uanlz_web
%dir /etc/uauditanalyzer3
%config(noreplace) /etc/uauditanalyzer3/*
/var/www/uauditweb3/*
/usr/lib/systemd/system/uanlz_alert.service
/usr/lib/systemd/system/uanlz_web.service
/usr/lib/systemd/system/uanlz_log2json.service
%post
%systemd_post uanlz_alert.service
%systemd_post uanlz_web.service
%systemd_post uanlz_log2json.service
%preun
%systemd_preun uanlz_alert.service
%systemd_preun uanlz_web.service
%systemd_preun uanlz_log2json.service
%postun
%systemd_postun_with_restart uanlz_alert.service
%systemd_postun_with_restart uanlz_web.service
%systemd_postun_with_restart uanlz_log2json.service
%changelog
