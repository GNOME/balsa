# Note that this is NOT a relocatable package
%define ver      0.0.3
%define rel      SNAP
%define prefix   /usr

Summary:   Balsa Mail Client
Name:      balsa
Version:   %ver
Release:   %rel
Copyright: GPL
Group:     Shells
Source0:   balsa-%{PACKAGE_VERSION}.tar.gz
URL:       http://www.serv.net/~jpaint/balsa/
BuildRoot: /tmp/balsa-%{PACKAGE_VERSION}-root
Packager: Michael Fulbright <msf@redhat.com>
Requires: gtk+ >= 0.99.5
Docdir: %{prefix}/doc

%description
Balsa is a e-mail reader. It uses the GTK toolkit, and a
C mail reading library (called the c-client library) written by Mark
Crispin. It is being integrated into the GNOME desktop environment.
 
%prep
%setup

%build
(cd imap; make lnx)

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
* Thu Apr 02 1998 Michael Fulbright <msf@redhat.com>

- First try at an RPM




