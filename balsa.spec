# Note that this is NOT a relocatable package
%define ver      0.3.5
%define rel      SNAP
%define prefix   /usr

Summary:   Balsa Mail Client
Name:      balsa
Version:   %ver
Release:   %rel
Copyright: GPL
Group:     Shells
Source0:   balsa-%{PACKAGE_VERSION}.tar.gz
URL:       http://www.balsa.net/
BuildRoot: /tmp/balsa-%{PACKAGE_VERSION}-root
Packager: Michael Fulbright <msf@redhat.com>
Requires: gtk+ >= 1.1.0
Requires: gnome-libs
Docdir: %{prefix}/doc

%description
Balsa is a e-mail reader.  This client is part of the GNOME
desktop environment.  It supports local mailboxes, POP3 and
IMAP.
 
%prep
%setup

%build
CFLAGS="$RPM_OPT_FLAGS" LDFLAGS="-s" ./autogen.sh \
	--prefix=%{prefix} 

if [ "$SMP" != "" ]; then
  (make "MAKE=make -k -j $SMP"; exit 0)
  make
else
  make
fi

%install
rm -rf $RPM_BUILD_ROOT
#install -d $RPM_BUILD_ROOT/etc/{rc.d/init.d,pam.d,profile.d,X11/wmconfig}

make prefix=$RPM_BUILD_ROOT%{prefix} install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-, root, root)
%doc README COPYING ChangeLog NEWS TODO
%{prefix}/bin/balsa
%{prefix}/share/*


%changelog
* Sun Jul 26 1998 Stuart Parmenter <pavlov@pavlov.net>

- Updated RPM file to reflect recent changes with the
  removal of c-client.

* Thu Apr 02 1998 Michael Fulbright <msf@redhat.com>

- First try at an RPM




