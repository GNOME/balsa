dnl -*- mode: m4 -*-
dnl follow section 5.3 of Autoconf, Automake book.
dnl
dnl 1. Boilerplate.
dnl
AC_PREREQ([2.69])
dnl ###########################################################################
dnl Boilerplace and versioning
dnl ###########################################################################

# if autoconf is used not in a git source tree, use version=2.6.5
AC_INIT([balsa],
	m4_esyscmd(test -d .git && (V=`git tag --points-at`; test -n "$V" || V=`git describe --tags`; echo -n $V) || echo -n 2.6.5),
	[])

AM_INIT_AUTOMAKE
AC_LANG([C])
# Automake-1.11 option for less verbose output
# configure --disable-silent-rules to restore verbose
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AC_DEFINE_UNQUOTED(BALSA_VERSION, "AC_PACKAGE_VERSION",[Defines the Balsa version])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

dnl #####################################################################
dnl 2. Options
dnl #####################################################################
AC_ARG_WITH([gnome],
        AS_HELP_STRING([--with-gnome],
                [Use GNOME libraries (default=yes)]),[
	with_gnome=$withval
],[
	with_gnome=yes
])

dnl ###########################################################################
dnl Configure locking
dnl ###########################################################################

AC_ARG_ENABLE(flock,
        AS_HELP_STRING([--enable-flock],
                       [Do NOT use flock() to lock files (default=no)]),
	[if test x$enableval = xno; then balsa_cv_flock=no; fi])

if test x$balsa_cv_flock = xyes; then
	AC_DEFINE(USE_FLOCK,1,[Define if flock locking should be used.])
fi

balsa_cv_fcntl=yes
AC_ARG_ENABLE(fcntl,
        AS_HELP_STRING([--disable-fcntl],
                       [Use fcntl() to lock files (default=yes)]),
	[if test x$enableval = no; then balsa_cv_fcntl=no; fi])
if test x$balsa_cv_fcntl = xyes; then
	AC_DEFINE(USE_FCNTL,1,[Define if fcntl locking should be used.])
fi

AC_ARG_ENABLE([autocrypt],
   AS_HELP_STRING([--enable-autocrypt],
                  [build with Autocrypt support (see https://autocrypt.org/), default=no, requires sqlite3)]),
                  [autocrypt=$enableval], [autocrypt=no])

AC_ARG_ENABLE([systray],
   AS_HELP_STRING([--enable-systray],
                  [enable System Tray Icon support, default=no, requires libxapp]),
                  [systray=$enableval], [systray=no])

AC_ARG_WITH(canberra,
   AS_HELP_STRING([--with-canberra],
                  [Use libcanberra-gtk3 for new messages and filter sounds (default=no)]),
                  [with_canberra=$withval],[with_canberra=no])

AC_ARG_WITH(compface,
   AS_HELP_STRING([--with-compface],
                  [Enable Compface (default=no)]),
                  [with_compface=$withval],[with_compface=no])

AC_ARG_WITH(gss,
   AS_HELP_STRING([--with-gss],
                  [Enable GSS (default=no)]),
                  [with_gss=$withval],[with_gss=no])

AC_ARG_WITH(html-widget,
            AS_HELP_STRING([--with-html-widget=(no|webkit2)],
                           [select the HTML renderer (default webkit2)]),
            [use_html_widget=$withval],
            [use_html_widget=webkit2])

AC_ARG_WITH([gtksourceview],
   AS_HELP_STRING([--with-gtksourceview],
                  [Use GtkSourceView-3 if available (default=no)]),
		  [with_gtksourceview=$withval],[with_gtksourceview=no])

AC_ARG_WITH([spell-checker],
            AS_HELP_STRING([--with-spell-checker=(internal|gtkspell|gspell)],
                           [select the spell checker (default internal)]),
            [use_spell_checker=$withval],
            [use_spell_checker=internal])

AC_ARG_WITH([ldap],
   AS_HELP_STRING([--with-ldap=DIR],
                  [Use OpenLDAP if available (default=no)]),
		  [with_ldap=$withval],[with_ldap=no])

AC_ARG_WITH([osmo],
   AS_HELP_STRING([--with-osmo],
                  [add Osmo address book support (experimental, needs osmo > svn rev. 1099, default=no)]),
		  [with_osmo=$withval],[with_osmo=no])

AC_ARG_WITH([webdav],
   AS_HELP_STRING([--with-webdav],
                  [add WebDAV support: CardDAV address book (requires libsoup and libxml2, default=no)]),
                  [with_webdav=$withval],[with_webdav=no])

AC_ARG_WITH([gpe],
   AS_HELP_STRING([--with-gpe],
                  [add support for GPE address books (requires sqlite3, default=no)]),
                  [with_gpe=$withval],[with_gpe=no])

AC_ARG_WITH(libsecret,
   AS_HELP_STRING([--with-libsecret],
                  [Use libsecret to store credentials (default=yes)]),
                  [with_libsecret=$withval],[with_libsecret=yes])

AC_ARG_WITH(gcr,
   AS_HELP_STRING([--with-gcr],
                  [Use libgcr-3 for dealing with TLS certificates (experimental, default=no)]),
                  [with_gcr=$withval],[with_gcr=no])

AC_ARG_ENABLE(more-warnings,
   AS_HELP_STRING([--enable-more-warnings],
                  [Enable maximum compiler warnings (default=yes)]),
                  [set_more_warnings="$enableval"], [set_more_warnings=yes])

dnl Documentation and Testing requirements for libnetclient
AC_ARG_WITH(libnetclient-docs,
   AS_HELP_STRING([--with-libnetclient-docs],
                  [Check requirements for building the libnetclient API docs (see libnetclient/README; default=no)]),
                  [with_libncdocs=$withval],[with_libncdocs=no])

AC_ARG_WITH(libnetclient-test,
   AS_HELP_STRING([--with-libnetclient-test],
                  [Check requirements for running libnetclient tests (see libnetclient/README; default=no)]),
                  [with_libncdtest=$withval],[with_libncdtest=no])


dnl #####################################################################
dnl 3. Programs: compilers and their options.
dnl #####################################################################
AC_PROG_CC
m4_ifdef([LT_INIT],[LT_INIT],[LT_INIT])
AC_PROG_LN_S
AC_CHECK_MEMBERS([struct utsname.domainname],[],[],[[#include<sys/utsname.h>]])

# pkg-config
#
PKG_PROG_PKG_CONFIG

# glib-compile-resources
#
AC_PATH_PROG([GLIB_COMPILE_RESOURCES], [glib-compile-resources])

# yelp
#
AC_MSG_CHECKING([for Yelp tool])
YELPTOOL=`which yelp-build 2>/dev/null`
if test -z $YELPTOOL; then
    AC_MSG_RESULT([no])
    echo "*** The tools to build the documentation are not found."
    echo "    To build it, please install the yelp-tools package."
    echo "    Continuing with configure. ****"
    AM_CONDITIONAL([HAVE_YELPTOOL], [false])
else
    AC_MSG_RESULT([yes])
    YELP_HELP_INIT
    AM_CONDITIONAL([HAVE_YELPTOOL], [true])
fi

# gettext
#
AM_NLS
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.21])
if test x$USE_NLS = xyes; then
    ALL_LINGUAS=`ls po/*.po | sed -e 's/po\///' -e 's/.po//' | tr '\n' ' '`
    AC_MSG_NOTICE([Gettext languages: $ALL_LINGUAS])
    GETTEXT_PACKAGE=balsa
    AC_SUBST(GETTEXT_PACKAGE)
    AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE",
            [The gettext package name.])
    AC_CHECK_HEADERS([locale.h])

    # _NL_MEASUREMENT_MEASUREMENT is an enum and not a define
    AC_MSG_CHECKING([for _NL_MEASUREMENT_MEASUREMENT])
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <langinfo.h>]],
                                    [[char c;
                                      c = *((unsigned char *)
                                      nl_langinfo
                                      (_NL_MEASUREMENT_MEASUREMENT));]])],
                   [have__nl_measurement_measurement=yes],
                   [have__nl_measurement_measurement=no])
    AC_MSG_RESULT($have__nl_measurement_measurement)
    if test "$have__nl_measurement_measurement" = "yes"; then
      AC_DEFINE([HAVE__NL_MEASUREMENT_MEASUREMENT], [1],
	        [Define if _NL_MEASUREMENT_MEASUREMENT is available])
    fi
fi

dnl #####################################################################
dnl GNOME libs
dnl #####################################################################

if test x$with_gnome != xno; then
   TOOLKIT_CATEGORIES="GNOME;GTK;"
else
   TOOLKIT_CATEGORIES="GTK;"
fi
AC_SUBST(TOOLKIT_CATEGORIES)

dnl #####################################################################
dnl 4. Libraries.
dnl #####################################################################

PKG_CHECK_MODULES(BALSA, [
glib-2.0 >= 2.48.0
gtk+-3.0 >= 3.24.0
gmime-3.0 >= 3.2.6
gio-2.0
gthread-2.0
gnutls >= 3.0
fribidi
libical >= 2.0.0
])

PKG_CHECK_MODULES(BALSA_AB, [
   glib-2.0
   gtk+-3.0
   gmime-3.0 >= 3.2.6
])

PKG_CHECK_MODULES(LIBNETCLIENT, [
	glib-2.0
	gio-2.0
	gnutls
])

dnl ##########################################################################
dnl Check versions
dnl ##########################################################################

AC_SUBST(BALSA_CFLAGS)
AC_SUBST(BALSA_LIBS)
AC_SUBST(BALSA_AB_LIBS)
AC_SUBST(BALSA_DEFS)

# HTML widget
#
AC_MSG_CHECKING(whether to use an HTML widget)
case "$use_html_widget" in
    webkit2)
        AC_MSG_RESULT([$use_html_widget])
        # note: sqlite3 is needed to manage html vs. plain and image download preferences
        PKG_CHECK_MODULES(HTML, [ webkit2gtk-4.1
                                  sqlite3 >= 3.24.0], , [
            PKG_CHECK_MODULES(HTML, [ webkit2gtk-4.0 >= 2.28.0
                                      sqlite3 >= 3.24.0])])
        BALSA_WEB_EXTENSIONS="${libdir}/${PACKAGE}"
        AC_SUBST(BALSA_WEB_EXTENSIONS)
        CPPFLAGS="$CPPFLAGS -DBALSA_WEB_EXTENSIONS=\\\"$BALSA_WEB_EXTENSIONS\\\""
        AC_PATH_PROGS(HTML2TEXT,
                      [python-html2text  \
                       html2markdown     \
                       html2markdown.py2 \
                       html2markdown.py3 \
                       html2text],
                      [no])
        if test x"$HTML2TEXT" != "xno" ; then
            AC_DEFINE_UNQUOTED(HTML2TEXT, "$HTML2TEXT",
				[Path to html2text program.])
			AC_MSG_CHECKING([whether $HTML2TEXT accepts the --unicode-snob option])
			echo "<html/>" > conftest.htm
			$HTML2TEXT --unicode-snob conftest.htm > /dev/null 2>&1
			if test $? = 0 ; then
				AC_MSG_RESULT([yes])
				AC_DEFINE(HTML2TEXT_UCOPT,1,[Defined when $HTML2TEXT accepts the --unicode-snob option.])
			else
				AC_MSG_RESULT([no])
			fi
			rm -f conftest.htm
		else
			AC_MSG_WARN([You cannot properly quote html-only messags.])
		fi
    ;;
    no)
        AC_MSG_RESULT([none])
    ;;
    *) AC_MSG_ERROR([bad option $use_html_widget]) ;;
esac

if test x"$use_html_widget" != xno ; then
    AC_DEFINE(HAVE_HTML_WIDGET,1,[Defined when an HTML widget can be used.])
    BALSA_CFLAGS="$BALSA_CFLAGS $HTML_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $HTML_LIBS"
fi


# GPGME detection.
#
PKG_CHECK_MODULES(GPGME, [ gpgme >= 1.8.0 ])
BALSA_CFLAGS="$BALSA_CFLAGS $GPGME_CFLAGS"
BALSA_LIBS="$BALSA_LIBS $GPGME_LIBS"

# Autocrypt support
AC_MSG_CHECKING(whether to build with Autocrypt support)
if test x"$autocrypt" != xno ; then
	AC_MSG_RESULT([yes])
	PKG_CHECK_MODULES(AUTOCRYPT, [sqlite3])
	AC_DEFINE(ENABLE_AUTOCRYPT,1,[If defined, enable Autocrypt support])
	BALSA_CFLAGS="$BALSA_CFLAGS $AUTOCRYPT_CFLAGS"
	BALSA_LIBS="$BALSA_LIBS $AUTOCRYPT_LIBS"
else
	AC_MSG_RESULT([no])
fi

# System Tray icon support via libxapp
AC_MSG_CHECKING(whether to build with System Tray Icon support)
if test x"$systray" != xno ; then
	AC_MSG_RESULT([yes])
	PKG_CHECK_MODULES(SYSTRAY, [xapp])
	AC_DEFINE(ENABLE_SYSTRAY,1,[If defined, enable System Tray Icon support])
	BALSA_CFLAGS="$BALSA_CFLAGS $SYSTRAY_CFLAGS"
	BALSA_LIBS="$BALSA_LIBS $SYSTRAY_LIBS"
else
	AC_MSG_RESULT([no])
fi

# OpenLDAP configuration.
#
AC_MSG_CHECKING(whether to use LDAP)
if test x"$with_ldap" != xno ; then
	AC_MSG_RESULT([yes])
	AC_CHECK_LIB(ldap, ldap_search, AC_DEFINE(ENABLE_LDAP, 1,
                 [Defined when the LDAP support is to be compiled in.])
              LIBS="-lldap -llber -lresolv $LIBS", AC_MSG_ERROR([*** You enabled LDAP but ldap library is not found.]), -llber -lresolv)
        AC_CHECK_LIB(ldap, ldap_start_tls_s,
                     AC_DEFINE(HAVE_LDAP_TLS,1,
                               [Defined when LDAP lib supports TLS]))
    if test "$with_ldap" != yes; then
       LIBS="$LIBS -L$with_ldap/lib"
       CPPFLAGS="$CPPFLAGS -I$with_ldap/include"
    fi
else
	AC_MSG_RESULT([no])
fi

# GPE address book configuration.
#
AC_MSG_CHECKING(whether to include GPE address book support)
if test x$with_gpe != xno ; then
    AC_MSG_RESULT([yes])
    PKG_CHECK_MODULES(SQLITE3, [sqlite3])
    AC_DEFINE(HAVE_GPE,1,[If defined, enable GPE address book support.])
    BALSA_CFLAGS="$BALSA_CFLAGS $SQLITE3_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $SQLITE3_LIBS"
    BALSA_AB_LIBS="$BALSA_AB_LIBS $SQLITE3_LIBS"
else
    AC_MSG_RESULT([no])
fi

# Osmo address book support
AC_MSG_CHECKING(whether to include experimental Osmo support)
if test x$with_osmo != xno ; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(HAVE_OSMO,1,[If defined, enable Osmo address book support.])
else
    AC_MSG_RESULT([no])
fi

# KRB5/GSSAPI configuration.
#
if test "x$with_gss" != xno  ; then
   AC_PATH_PROG(KRB5_CONFIG, krb5-config,no,[${PATH}:/usr/kerberos/bin:/usr/lib/mit/bin])
   if test "x$KRB5_CONFIG" != xno ; then 
      AC_DEFINE(HAVE_GSSAPI,1,[Defined when GSSAPI support is to be compiled.])
      BALSA_CFLAGS="$BALSA_CFLAGS `$KRB5_CONFIG --cflags gssapi`"
      BALSA_LIBS="$BALSA_LIBS `$KRB5_CONFIG --libs gssapi`"
      LIBNETCLIENT_CFLAGS="$LIBNETCLIENT_CFLAGS `$KRB5_CONFIG --cflags gssapi`"
      LIBNETCLIENT_LIBS="$LIBNETCLIENT_LIBS `$KRB5_CONFIG --libs gssapi`"
      SAVE_CFLAGS="$CFLAGS"; CFLAGS="$BALSA_CFLAGS"
      AC_CHECK_HEADER([gssapi.h],
                      [AC_DEFINE([HAVE_HEIMDAL],1,[Defined for Heimdal.])],
        [AC_CHECK_HEADER([gssapi/gssapi.h],,
                         AC_MSG_ERROR([gssapi.h not found]))])
      CFLAGS="$SAVE_CFLAGS"
   else
      AC_MSG_ERROR([GSS requested but krb5-config not found. Install krb5-devel/heimdal-devel])
   fi
fi

# gcr configuration
if test x$with_gcr != xno; then
    PKG_CHECK_MODULES(GCR, [ gcr-3 ])
    AC_DEFINE(HAVE_GCR,1,[Defined when gcr-3 can be used.])
    BALSA_CFLAGS="$BALSA_CFLAGS $GCR_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $GCR_LIBS"
fi

# Compface configuration
#
AC_MSG_CHECKING([whether to build Compface support])
if test "x$with_compface" != xno  ; then
    AC_MSG_RESULT([yes])
    test "$with_compface" != yes && BALSA_LIBS="$BALSA_LIBS -L$with_compface/lib"
    AC_CHECK_LIB(compface, uncompface, BALSA_LIBS="$BALSA_LIBS -lcompface",
	AC_MSG_ERROR([*** You enabled Compface but the library is not found.]),)
    AC_DEFINE(HAVE_COMPFACE, 1, 
              [Defined when Compface support is requested.])
    test "$with_compface" != yes && CPPFLAGS="$CPPFLAGS -I$with_compface/include"
else
    AC_MSG_RESULT([no])
fi

# GtkSourceView configuration
#
if test x$with_gtksourceview != xno; then
    PKG_CHECK_MODULES([GTKSOURCEVIEW], [ gtksourceview-4 ])
    AC_DEFINE(HAVE_GTKSOURCEVIEW,1,[Defined when GtkSourceView-4 can be used.])
    BALSA_CFLAGS="$BALSA_CFLAGS $GTKSOURCEVIEW_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $GTKSOURCEVIEW_LIBS"
fi
AM_CONDITIONAL([BUILD_WITH_GTKSOURCEVIEW], [test x$with_gtksourceview != xno ])

# libcanberra configuration
#
AC_MSG_CHECKING([whether to build with canberra])
if test x$with_canberra != xno; then
    AC_MSG_RESULT([yes])
    PKG_CHECK_MODULES(CANBERRA, [ libcanberra-gtk3 ])
    AC_DEFINE(HAVE_CANBERRA,1,[Defined when canberra can be used.])
    BALSA_CFLAGS="$BALSA_CFLAGS $CANBERRA_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $CANBERRA_LIBS"
else
    AC_MSG_RESULT([no])
fi

# Libsecret configuration
#
AC_MSG_CHECKING([whether to configure for libsecret])
if test x$with_libsecret != xno; then
    AC_MSG_RESULT([yes])
    PKG_CHECK_MODULES(LIBSECRET, [ libsecret-1 ])
    AC_DEFINE(HAVE_LIBSECRET,1,[Defined when libsecret can be used.])
    BALSA_CFLAGS="$BALSA_CFLAGS $LIBSECRET_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $LIBSECRET_LIBS"
    BALSA_AB_LIBS="$BALSA_AB_LIBS $LIBSECRET_LIBS"
else
    AC_MSG_RESULT([no])
    AC_MSG_WARN([libsecret support is disabled. Balsa will store the
		the obfuscated passwords in ~/.balsa/config-private,
		which is usually not what you want.])
fi

# WebDAV (CardDAV) support
#
AC_MSG_CHECKING(whether to include WebDAV (CardDAV) support)
if test x$with_webdav != xno ; then
    AC_MSG_RESULT([yes])
    PKG_CHECK_MODULES(WEBDAV, [
        libsoup-3.0
        libxml-2.0
    ], , [
        PKG_CHECK_MODULES(WEBDAV, [
            libsoup-2.4
            libxml-2.0
        ])])
    AC_DEFINE(HAVE_WEBDAV,1,[If defined, enable WebDAV (CardDAV) support.])
    BALSA_CFLAGS="$BALSA_CFLAGS $WEBDAV_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $WEBDAV_LIBS"
    BALSA_AB_LIBS="$BALSA_AB_LIBS $WEBDAV_LIBS"
else
    AC_MSG_RESULT([no])
fi

dnl #####################################################################
dnl 5. Headers.
dnl #####################################################################

# Spell check detection.
#
AC_MSG_CHECKING(whether to use an external spell checker)
case "$use_spell_checker" in
    gspell)
    AC_MSG_RESULT([configure gspell])
    PKG_CHECK_MODULES(SPELL, [ gspell-1 >= 1.6.0 ])
    AC_DEFINE(HAVE_GSPELL,1,[Defined when Gspell can be used.])
    ;;
    gtkspell)
    AC_MSG_RESULT([configure gtkspell])
    PKG_CHECK_MODULES(SPELL, [ gtkspell3-3.0 >= 3.0.9 ])
    AC_DEFINE(HAVE_GTKSPELL,1,[Defined when GtkSpell can be used.])
    ;;
    internal)
    AC_MSG_RESULT([configure enchant])
    if $PKG_CONFIG enchant-2 ; then
        PKG_CHECK_MODULES(SPELL, [ enchant-2 ])
    else
        PKG_CHECK_MODULES(SPELL, [ enchant != 1.6.1 ])
    fi
    ;;
    *)
    AC_MSG_ERROR([bad option $use_spell_checker])
    ;;
esac
BALSA_CFLAGS="$BALSA_CFLAGS $SPELL_CFLAGS"
BALSA_LIBS="$BALSA_LIBS $SPELL_LIBS"
AM_CONDITIONAL([BUILD_WITH_SPELL_CHECKER], [test $use_spell_checker != "internal"])

dnl #####################################################################
dnl 6. Typedefs, structures and compiler characteristics.
dnl #####################################################################
AC_C_CONST

AC_CHECK_DECLS([ctime_r], [], [], [[#include <time.h>]])
AC_CHECK_FUNCS([ctime_r])

# more warnings.
#

if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
	for option in -Wsign-compare -Wpointer-arith -Wnested-externs \
            -Wchar-subscripts -Wuninitialized -Wmissing-prototypes    \
            -Wreturn-type -Wdeclaration-after-statement \
            -Wredundant-decls -Wshadow -Wformat -Wformat-security ; do
                SAVE_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[has_option=yes],[has_option=no])
                if test $has_option = no; then
                  CFLAGS="$SAVE_CFLAGS"
                fi
                AC_MSG_RESULT($has_option)
                unset has_option
                unset SAVE_CFLAGS
        done
        unset option
fi

# extra warnings for GIT.
#

if test -d "${srcdir}/.git" ; then
	BALSA_FROM_GIT=yes
	BALSA_CFLAGS="$BALSA_CFLAGS -Wall -Werror"
        BALSA_DEFS="$BALSA_DEFS \
-DGDK_DISABLE_DEPRECATED \
-DGMIME_DISABLE_DEPRECATED \
-DGTK_DISABLE_DEPRECATED \
-DNM_DISABLE_DEPRECATED \
-DPANGO_DISABLE_DEPRECATED \
-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 \
"
else
	BALSA_FROM_GIT=no
fi

AC_MSG_CHECKING([for GIT information])
AC_MSG_RESULT($BALSA_FROM_GIT)

dnl ###########################################################################
dnl Remember our prefixes
dnl ###########################################################################

if test "x$prefix" = "xNONE"; then
	prefix=$ac_default_prefix
fi
BALSA_DATA_PREFIX="${datadir}/balsa"
AC_SUBST(BALSA_DATA_PREFIX)
CPPFLAGS="$CPPFLAGS -DBALSA_DATA_PREFIX=\\\"${BALSA_DATA_PREFIX}\\\""
AC_PATH_PROG(GTK_UPDATE_ICON_CACHE, gtk-update-icon-cache, no)

dnl Documentation and Testing requirements for libnetclient

if test x$with_libncdocs != xno; then
	AC_PATH_PROGS(DOXYGEN, doxygen, no)
	if test "x$DOXYGEN" = "xno" ; then
		AC_MSG_ERROR([*** Cannot build libnetclient API documentation, see libnetclient/README for fixing this issue ***])
	fi
fi

if test x$with_libncdtest != xno; then
	AC_CHECK_HEADER([sput.h],[have_sput_h=yes],[have_sput_h=no])
	AC_PATH_PROGS(VALGRIND, valgrind, no)
	AC_PATH_PROGS(LCOV, lcov, no)
	AC_PATH_PROGS(GENHTML, genhtml, no)
	AC_PATH_PROGS(SCREEN, screen, no)
	AC_PATH_PROGS(GTLSSRV, gnutls-serv, no)
	AC_PATH_PROGS(SUDO, sudo, no)
	AC_PATH_PROGS(INETSIM, inetsim, no)
	if test "x$have_sput_h" = "xno" || \
	   test "x$VALGRIND" = "xno" || \
	   test "x$LCOV" = "xno" || \
	   test "x$GENHTML" = "xno" || \
	   test "x$SCREEN" = "xno" || \
	   test "x$GTLSSRV" = "xno" || \
	   test "x$SUDO" = "xno" || \
	   test "x$INETSIM" = "xno" ; then
	    AC_MSG_ERROR([*** Cannot run libnetclient tests, see libnetclient/README for fixing this issue ***])
	fi
fi

dnl #####################################################################
dnl 8. Output
dnl #####################################################################

AC_CONFIG_FILES([
po/Makefile.in
Makefile
balsa.1
balsa.spec
org.desktop.Balsa.desktop.in
balsa-mailto-handler.desktop.in
sounds/Makefile
images/Makefile
images/mimetypes/Makefile
images/16x16/Makefile
images/24x24/Makefile
doc/Makefile
libbalsa/Makefile
libbalsa/imap/Makefile
libinit_balsa/Makefile
libnetclient/Makefile
libnetclient/test/Makefile
libnetclient/test/start-test-env.sh
src/Makefile
ui/Makefile
],
[test -f libnetclient/test/start-test-env.sh && chmod +x libnetclient/test/start-test-env.sh])
AC_OUTPUT

dnl ##########################################################################
dnl Summary
dnl ##########################################################################

echo ""
echo "================ Final configuration ==================="
echo "    Installing into prefix: $prefix"
echo "   Enable compile warnings: $set_more_warnings"
echo "   Native Language Support: $USE_NLS"
echo "               HTML widget: $use_html_widget"
echo "                 Use GNOME: $with_gnome"
echo "              Use Canberra: $with_canberra"
echo "             Use Autocrypt: $autocrypt"
echo "          System Tray Icon: $systray"
echo "                  Use LDAP: $with_ldap"
echo "                   Use GSS: $with_gss"
echo "          GPE address book: $with_gpe"
echo "            WebDAV support: $with_webdav"
echo "             Spell checker: $use_spell_checker"
echo "         Use GtkSourceView: $with_gtksourceview"
echo "              Use Compface: $with_compface"
echo "             Use libsecret: $with_libsecret"
echo "                   Use gcr: $with_gcr"
dnl echo "                  Use SASL: $need_sasl"
echo ""

if test x"$BALSA_FROM_GIT" = xyes ; then
	echo "============================== NOTICE ================================"
	echo "                You are using Balsa from GIT source."
	echo "  The program is likely to be unstable, contain incomplete features,"
	echo "or just plain not work. Use it at your own risk. You have been warned."
	echo "======================================================================"
	echo ""
fi
