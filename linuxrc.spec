#
# spec file for package linuxrc
#
# Copyright (c) 2016 SUSE LINUX Products GmbH, Nuernberg, Germany.
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via http://bugs.opensuse.org/
#


Name:           linuxrc
BuildRequires:  e2fsprogs-devel
BuildRequires:  hwinfo-devel
BuildRequires:  libblkid-devel
BuildRequires:  libcurl-devel
BuildRequires:  readline-devel
Summary:        SUSE Installation Program
License:        GPL-3.0+
Group:          System/Boot
Version:        5.0.86
Release:        0
Source:         %{name}-%{version}.tar.xz
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
SUSE installation program.



Authors:
--------
    Hubert Mantel <mantel@suse.de>

%prep
%setup

%build
  make

%install
  install -d -m 755 %{buildroot}/usr/{s,}bin
  make install DESTDIR=%{buildroot}

%clean 
rm -rf %{buildroot}

%files
%defattr(-,root,root)
/usr/sbin/linuxrc
/usr/bin/mkpsfu
/usr/share/linuxrc
%doc linuxrc.html

%changelog
