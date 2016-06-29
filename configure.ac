dnl -*- mode: m4 -*-
dnl follow section 5.3 of Autoconf, Automake book.
dnl
dnl 1. Boilerplate.
dnl
AC_PREREQ(2.59)
dnl ###########################################################################
dnl Boilerplace and versioning
dnl ###########################################################################

# if autoconf is used not in a git source tree, use version=2.5.2
AC_INIT([balsa],
        m4_esyscmd([test -d .git && echo -n `git describe --tags` || echo -n 2.5.2]),
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
        AC_HELP_STRING([--with-gnome],
                [Use GNOME libraries (default=yes)]),[
	with_gnome=$withval
],[
	with_gnome=yes
])

dnl ###########################################################################
dnl Configure locking
dnl ###########################################################################

AC_ARG_ENABLE(flock,
        AC_HELP_STRING([--enable-flock],
                       [Do NOT use flock() to lock files (default=no)]),
	[if test x$enableval = xno; then balsa_cv_flock=no; fi])

if test x$balsa_cv_flock = xyes; then
	AC_DEFINE(USE_FLOCK,1,[Define if flock locking should be used.])
fi

balsa_cv_fcntl=yes
AC_ARG_ENABLE(fcntl,
        AC_HELP_STRING([--disable-fcntl],
                       [Use fcntl() to lock files (default=yes)]),
	[if test x$enableval = no; then balsa_cv_fcntl=no; fi])
if test x$balsa_cv_fcntl = xyes; then
	AC_DEFINE(USE_FCNTL,1,[Define if fcntl locking should be used.])
fi

AC_ARG_ENABLE(threads,
        AC_HELP_STRING([--enable-threads],
                       [Use threading for mail retrieval (default=yes)]),[
	use_threads=$enableval
],[
	use_threads=yes
])

if test x"$use_threads" = xyes; then 
        AC_DEFINE(BALSA_USE_THREADS, 1,[Defined when balsa is to use threads.])
fi

AC_ARG_ENABLE(debug-threads,
        AC_HELP_STRING([--enable-debug-threads],
                       [Debug threads (default=no)]),[
	debug_threads=$enableval
],[
	debug_threads=no
])

if test x"$debug_threads" = xyes; then
        AC_DEFINE(BALSA_DEBUG_THREADS, 1,[Defined to debug threads.])
fi


AC_ARG_WITH([gpgme],
   AC_HELP_STRING([--with-gpgme=gpgme-config],
                  [build with gpgme/GnuPG support (default=no, path to gpgme-config optional)]),
	          [ gpgmecfg=$withval ], [ gpgmecfg=no ])
AC_ARG_ENABLE([smime],
   AC_HELP_STRING([--enable-smime],
                  [include S/MIME support (needs gpgme and gpgsm, experimental for gpg < 2.0.4)]),
	          [ have_smime=$enableval ], [ have_smime=check ])
AC_ARG_WITH([gpg-app],
   AC_HELP_STRING([--with-gpg-app=PATH],
                  [use PATH as GnuPG application (default=gpg2 if >= 2.0.4, otherwise gpg 1.x)]),
	          [ gpgapp=$withval ], [ gpgapp=no ])



AC_ARG_WITH(canberra,
   AC_HELP_STRING([--with-canberra],
                  [Use libcanberra-gtk3 for filter sounds (default=no)]),
                  [with_canberra=$withval],[with_canberra=no])

AC_ARG_WITH(compface,
   AC_HELP_STRING([--with-compface],
                  [Enable Compface (default=no)]),
                  [with_compface=$withval],[with_compface=no])

AC_ARG_WITH(esmtp,
   AC_HELP_STRING([--with-esmtp=DIR],
                  [Enable libESMTP (default=yes)]),
                  [ with_esmtp=$withval ],[ with_esmtp=yes ])

AC_ARG_WITH(gss,
   AC_HELP_STRING([--with-gss],
                  [Enable GSS (default=no)]),
                  [with_gss=$withval],[with_gss=no])

AC_ARG_WITH(html-widget,
            AC_HELP_STRING([--with-html-widget=(no|gtkhtml4|webkit|webkit2)],
                           [select the HTML renderer (default webkit)]),
            [use_html_widget=$withval],
            [use_html_widget=webkit])

AC_ARG_WITH([gtksourceview],
   AC_HELP_STRING([--with-gtksourceview],
                  [Use GtkSourceView-3 if available (default=no)]),
		  [with_gtksourceview=$withval],[with_gtksourceview=no])

AC_ARG_WITH([spell-checker],
            AC_HELP_STRING([--with-spell-checker=(no|gtkspell|gspell)],
                           [select the external spell checker (default no)]),
            [use_spell_checker=$withval],
            [use_spell_checker=no])

AC_ARG_WITH(iconv,      
   AC_HELP_STRING([--with-iconv=DIR],
                  [Where libiconv is installed]),
		  [if test "$with_iconv" != yes -a "$with_iconv" != no ; then
                   CPPFLAGS="-I${with_iconv}/include $CPPFLAGS"
                   LDFLAGS="-liconv -L${with_iconv}/lib $LDFLAGS"
		   fi])

AC_ARG_WITH([ldap],
   AC_HELP_STRING([--with-ldap=DIR],
                  [Use OpenLDAP if available (default=no)]),
		  [with_ldap=$withval],[with_ldap=no])

AC_ARG_WITH([libnotify],
   AC_HELP_STRING([--with-libnotify],
                  [Use libnotify if available (default=yes)]),
		  [with_libnotify=$withval],[with_libnotify=yes])

AC_ARG_WITH([macosx-desktop],
   AC_HELP_STRING([--with-macosx-desktop],
	          [Use Mac OS X Desktop Integration (default=no, extremely experimental)]),
		  [use_igemacint=$withval],[use_igemacint=no])

AC_ARG_WITH([rubrica],
   AC_HELP_STRING([--with-rubrica],
                  [add Rubrica2 address book support (needs libxml2, default=no)]),
		  [with_rubrica=$withval],[with_rubrica=no])

AC_ARG_WITH([sqlite],
   AC_HELP_STRING([--with-sqlite=DIR],
                  [Use SQLite for GPE address books (default=no)]),
                  [with_sqlite=$withval],[with_sqlite=no])

AC_ARG_WITH(libsecret,
   AC_HELP_STRING([--with-libsecret],
                  [Link to libsecret instead of gnome-keyring (default=no)]),
                  [with_libsecret=$withval],[with_libsecret=no])

AC_ARG_ENABLE(more-warnings,
   AC_HELP_STRING([--enable-more-warnings],
                  [Enable maximum compiler warnings (default=yes)]),
                  [set_more_warnings="$enableval"], [set_more_warnings=yes])



dnl #####################################################################
dnl 3. Programs: compilers and their options.
dnl #####################################################################
AC_PROG_CC
m4_ifdef([LT_INIT],[LT_INIT],[AC_PROG_LIBTOOL])
AC_PROG_LN_S
AC_CHECK_MEMBERS([struct utsname.domainname],[],[],[[#include<sys/utsname.h>]])

# pkg-config
#
AC_PATH_PROG(PKG_CONFIG, pkg-config, no)

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

# glib-genmarshal
#
AC_SUBST(GLIB_GENMARSHAL)
GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`

# gettext
#
ALL_LINGUAS="am ar az bg ca cs da de dz el en_CA en_GB es et eu fi fr ga gl he hi hr hu id it ja ko lt lv mk ml ms nb ne nl nn oc pa pl pt pt_BR ro ru rw sk sl sq sr sr@latin sv tr uk vi wa zh_CN zh_HK zh_TW"
GETTEXT_PACKAGE=balsa
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE",
        [The gettext package name.])

AC_PROG_INTLTOOL
AM_GLIB_GNU_GETTEXT

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

# sendmail
#
if test x"$with_esmtp" = xno; then
AC_PATH_PROG(SENDMAIL, sendmail, no, $PATH:/usr/sbin:/usr/lib)
AC_DEFINE_UNQUOTED(SENDMAIL, "$ac_cv_path_SENDMAIL",
		   [Path to sendmail program.])
fi

dnl #####################################################################
dnl GNOME libs
dnl #####################################################################

if test x$with_gnome != xno; then
   TOOLKIT_CATEGORIES="GNOME;GTK;"
   gnome_extras="$gnome_extras gio-unix-2.0"
   if test x$with_libsecret == xno; then
     AC_MSG_CHECKING([whether we have gnome-keyring])
     if $PKG_CONFIG --atleast-version=1.0 gnome-keyring-1; then
        gnome_extras="$gnome_extras gnome-keyring-1"
        AC_DEFINE(HAVE_GNOME_KEYRING,1,[Defined when gnome-keyring is there.])
        # Work around http://bugzilla.gnome.org/show_bug.cgi?id=556530
        if $PKG_CONFIG --atleast-version=2.24.1 gnome-keyring-1; then
           AC_DEFINE(HAVE_GNOME_KEYRING_24,1,
                     [Defined with gnome-keyring-2.24 or newer.])
        fi
        with_gnome_keyring=yes
     else
        with_gnome_keyring=no
     fi
     AC_MSG_RESULT($with_gnome_keyring)
   fi
   AC_DEFINE(HAVE_GNOME,1,[Defined when building for GNOME.])
else
   TOOLKIT_CATEGORIES="GTK;"
   gnome_extras=""
fi
AC_SUBST(TOOLKIT_CATEGORIES)

dnl #####################################################################
dnl 4. Libraries.
dnl #####################################################################

dnl Make sure ld finds zlib:
LIBS="$LIBS -lz"

PKG_CHECK_MODULES(BALSA, [
glib-2.0 >= 2.32.0
gtk+-3.0 >= 3.4.0
gmime-2.6
gio-2.0
gthread-2.0
$gnome_extras
])

PKG_CHECK_MODULES(BALSA_AB, [
   glib-2.0
   gtk+-3.0
   gmime-2.6
   $gnome_extras
])

AC_MSG_CHECKING(whether res_init is available)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <resolv.h>;]],
                               [[res_init();]])],
               have_res_init=yes,
               have_res_init=no)
if test $have_res_init = no ; then
    save_LIBS=$LIBS
    LIBS="$LIBS -lresolv"
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <resolv.h>;]],
                                   [[res_init();]])],
                   have_res_init=yes)
fi
AC_MSG_RESULT([$have_res_init])
if test $have_res_init = no ; then
    LIBS=$save_LIBS
else
    AC_DEFINE(HAVE_RES_INIT,1,
              [Defined when res_init is available.])
fi

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
    gtkhtml4)
        AC_MSG_RESULT([$use_html_widget])
        PKG_CHECK_MODULES(HTML, [ libgtkhtml-4.0 ], [],
          [PKG_CHECK_MODULES(HTML, [ libgtkhtml-4.0 ],[],
             [AC_MSG_ERROR([*** You enabled gtkhtml4 but it was not found.])])])
        AC_DEFINE(HAVE_GTKHTML4,1,[Defined when GtkHtml-4 can be used.])
    ;;
    webkit | webkit2)
        AC_MSG_RESULT([$use_html_widget])
        if test $use_html_widget = webkit2 ; then
            PKG_CHECK_MODULES(HTML, [ webkit2gtk-4.0 ])
            AC_DEFINE(USE_WEBKIT2,1,[Defined when WebKit2 is requested.])
        else
            if $PKG_CONFIG --atleast-version=1.5.1 webkitgtk-3.0 ; then
                PKG_CHECK_MODULES(HTML, [webkitgtk-3.0 javascriptcoregtk-3.0])
            else
                PKG_CHECK_MODULES(HTML, [webkitgtk-3.0])
            fi
        fi
        AC_DEFINE(HAVE_WEBKIT,1,[Defined when WebKit can be used.])
        AC_PATH_PROGS(HTML2TEXT,
                      [python-html2text  \
                       html2markdown     \
                       html2markdown.py2 \
                       html2markdown.py3 \
                       html2text],
                      no)
        if test x"$ac_cv_path_HTML2TEXT" != "xno" ; then
            AC_DEFINE_UNQUOTED(HTML2TEXT, "$ac_cv_path_HTML2TEXT",
				[Path to html2text program.])
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
    AC_DEFINE(HAVE_HTML_WIDGET,1,[Defined when any HTML widget can be used.])
    BALSA_CFLAGS="$BALSA_CFLAGS $HTML_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $HTML_LIBS"
fi


# GPGME detection.
# We don't use the std macro AM_PATH_GPGME here to avoid dependencies.

AC_MSG_CHECKING(whether to build with gpgme/GnuPG support)
if test x"$gpgmecfg" = xyes ; then
	AC_MSG_RESULT(yes)
	AC_PATH_PROG(gpgmecfg, gpgme-config, no)
	if test "$gpgmecfg" = no; then
	AC_MSG_ERROR([GPGme build requested but gpgme-config program not found.])
        fi
fi
if test x"$gpgmecfg" != xno ; then
	AC_MSG_CHECKING([gpgme library version])
	gpgmever=`$gpgmecfg --version`
	AC_MSG_RESULT($gpgmever)
        gpgme_ve=`echo $gpgmever|sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
        gpgme_ma=`echo $gpgmever|sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
        gpgme_mi=`echo $gpgmever|sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
	if test $gpgme_ve -lt 1; then
		gpgmecfg=no
		have_smime=no
	fi
	if test \( $gpgme_ve -eq 1 \) -a \( $gpgme_ma -lt 2 \) ; then
		gpgmecfg=no
		have_smime=no
	fi
	if test x"$gpgmecfg" != xno ; then
		if test x"$use_threads" = xyes; then
			BALSA_LIBS="$BALSA_LIBS `$gpgmecfg --thread=pthread --libs`"
			if test x"$?" != x0 ; then
				AC_MSG_ERROR([*** You enabled gpgme and pthread support, but your gpgme installation does not support threads.])
			fi
		else
			BALSA_LIBS="$BALSA_LIBS `$gpgmecfg --libs`"
		fi
		AC_DEFINE(HAVE_GPGME,1,[Defined when gpgme is available.])
		BALSA_CFLAGS="$BALSA_CFLAGS `$gpgmecfg --cflags`"
		gpgmecfg=yes
	else
		AC_MSG_WARN([sorry, you need at least gpgme version 1.2.0])
	fi
else
	have_smime=no
fi
AM_CONDITIONAL([BUILD_WITH_GPGME], [test $gpgmecfg = "yes"])

# check the GnuPG engine application version
# note: gpg2 interacts fine with gpgme only since version 2.0.4
AC_DEFUN([AC_CHECK_GPG_VER],[
	if ! test -x $1 ; then
		AC_MSG_ERROR([$1 is not executable])
	fi
	AC_MSG_CHECKING([$1 version])
	gpgver=`$1 --version | sed -e 's/.* //' -e q`
	AC_MSG_RESULT($gpgver)
        gpg_ve=`echo $gpgver|sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
        gpg_ma=`echo $gpgver|sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
        gpg_mi=`echo $gpgver|sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
	HAVE_GPG2=no
	if test \( $gpg_ve -eq 2 \) -a \( $gpg_ma -eq 0 \) -a \( $gpg_mi -lt 4 \) ; then
    		ifelse([$3], , :, [$3])
	else
		if test $gpg_ve -eq 2 ; then
			HAVE_GPG2=yes
		fi
    		ifelse([$2], , :, [$2])
	fi])

# find a suitable GnuPG engine if gpgme is enabled
if test x"$gpgmecfg" != xno ; then
	if test x"$gpgapp" != xno ; then
		AC_CHECK_GPG_VER($gpgapp, , 
				 AC_MSG_ERROR([gpg2 < 2.0.4 does not communicate properly with gpgme]))
	else
		AC_PATH_PROG(gpg2, gpg2, [no])
		if test x"$gpg2" != xno ; then
			AC_CHECK_GPG_VER($gpg2, [gpgapp=$gpg2],
					 AC_MSG_WARN([gpg2 < 2.0.4 does not communicate properly with gpgme]))
		fi
		if test x"$gpgapp" = xno ; then
			AC_PATH_PROG(gpg, gpg, [no])
			if test x"$gpg" != xno ; then
				AC_CHECK_GPG_VER($gpg, [gpgapp=$gpg],
					 	AC_MSG_WARN([gpg2 < 2.0.4 does not communicate properly with gpgme]))
			fi
		fi
		if test x"$gpgapp" = xno ; then
			AC_PATH_PROG(gpg1, gpg1, [no])
			if test x"$gpg1" != xno ; then
				AC_CHECK_GPG_VER($gpg1, [gpgapp=$gpg1],
					 	AC_MSG_WARN([gpg2 < 2.0.4 does not communicate properly with gpgme]))
			fi
		fi
	fi
	if test x"$gpgapp" != xno ; then
		AC_DEFINE(HAVE_GPG,1,[Defined when gpg is available.])
		AC_DEFINE_UNQUOTED(GPG_PATH,["$gpgapp"],[Path of gpg.])
	else
		AC_MSG_WARN([cannot find a suitable gpg application])
		AC_MSG_WARN([please consider using --with-gpg-app to specify it])
	fi
else
	AC_MSG_RESULT([no])
fi

# s/mime is mature for gpg >= 2.0.4
AC_MSG_CHECKING(whether to include S/MIME support)
if test \( x"$have_smime" = xyes \) -o \( \( x"$HAVE_GPG2" = xyes \) -a \( x"$have_smime" = xcheck \) \) ; then
	AC_MSG_RESULT([yes])
	have_smime=yes
	AC_DEFINE(HAVE_SMIME,1,[Defined when supporting S/MIME])
else
	AC_MSG_RESULT([no])
fi
AM_CONDITIONAL([BUILD_WITH_SMIME], [test $have_smime = "yes"])


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

# SQLite configuration.
#
AC_MSG_CHECKING(whether to use SQLite)
if test x$with_sqlite != xno ; then
    AC_MSG_RESULT([yes])
    if $PKG_CONFIG --exists sqlite3 ; then
        PKG_CHECK_MODULES(SQLITE, [sqlite3])
        AC_DEFINE(HAVE_SQLITE3,1,[If defined, use sqlite3 api.])
    else
        PKG_CHECK_MODULES(SQLITE, [sqlite >= 2.8])
    fi
    AC_DEFINE(HAVE_SQLITE,1,[If defined, enable GPE address book support.])
    BALSA_CFLAGS="$BALSA_CFLAGS $SQLITE_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $SQLITE_LIBS"
    BALSA_AB_LIBS="$BALSA_AB_LIBS $SQLITE_LIBS"
else
    AC_MSG_RESULT([no])
fi

# Rubrica (libxml2) configuration
#
AC_MSG_CHECKING(whether to include Rubrica2 support)
if test x$with_rubrica != xno ; then
    AC_MSG_RESULT([yes])
    PKG_CHECK_MODULES(LIBXML2, [libxml-2.0])
    AC_DEFINE(HAVE_RUBRICA,1,[If defined, enable Rubrica2 address book support.])
    BALSA_CFLAGS="$BALSA_CFLAGS $LIBXML2_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $LIBXML2_LIBS"
    BALSA_AB_LIBS="$BALSA_AB_LIBS $LIBXML2_LIBS"
else
    AC_MSG_RESULT([no])
fi

# LibESMTP configuration.
#
AC_MSG_CHECKING([whether to build ESMTP support])
if test "x$with_esmtp" != xno  ; then
    AC_MSG_RESULT([yes])
    test "$with_esmtp" != yes && LIBS="$LIBS -L$with_esmtp/lib"
    AC_CHECK_LIB(esmtp, smtp_start_session,BALSA_LIBS="$BALSA_LIBS -lesmtp",
	AC_MSG_ERROR([*** You enabled ESMTP but esmtp library is not found.]),
                 -lpthread)
    AC_CHECK_LIB(esmtp, smtp_option_require_all_recipients, 
                 AC_DEFINE(ENABLE_ESMTP, 1, 
                           [Defined when ESTMP support is to be enabled.]),
                 AC_MSG_ERROR([*** You enabled ESMTP but esmtp library
                               does not support 'require_all_recipients'.
                               Please get a Balsa-aware libESMTP from
                               http://pawsa.fedorapeople.org/balsa/download.html
                               or compile libESMTP with
                               './configure --enable-require-all-recipients'.]),
                 -lpthread)
    AC_CHECK_LIB(esmtp, smtp_starttls_set_password_cb,
	AC_DEFINE(HAVE_SMTP_TLS_CLIENT_CERTIFICATE,1,
	         [Defined when libESMTP has TLS support.]),
                 ,
		 -lpthread)
    test "$with_esmtp" != yes && CPPFLAGS="$CPPFLAGS -I$with_esmtp/include"
else
    AC_MSG_RESULT([no])
fi

# OpenSSL configuration.
#
PKG_CHECK_MODULES(OPENSSL, [openssl],
   [BALSA_CFLAGS="$OPENSSL_CFLAGS $BALSA_CFLAGS"
    BALSA_LIBS="$OPENSSL_LIBS $BALSA_LIBS"],
   [ AC_CHECK_HEADERS([openssl/ssl.h], [],
                      [AC_MSG_ERROR([OpenSSL headers not found])], [])
     AC_SEARCH_LIBS(SSL_library_init, ssl,
                    [BALSA_LIBS="$BALSA_LIBS -lssl -lcrypto"],
                    [AC_MSG_ERROR([libssl not found])], [-lcrypto]) ]
)

# KRB5/GSSAPI configuration.
#
if test "x$with_gss" != xno  ; then
   AC_PATH_PROG(KRB5_CONFIG, krb5-config,no,[${PATH}:/usr/kerberos/bin:/usr/lib/mit/bin])
   if test "x$KRB5_CONFIG" != xno ; then 
      AC_DEFINE(HAVE_GSSAPI,1,[Defined when GSSAPI support is to be compiled.])
      BALSA_CFLAGS="$BALSA_CFLAGS `$KRB5_CONFIG --cflags gssapi`"
      BALSA_LIBS="$BALSA_LIBS `$KRB5_CONFIG --libs gssapi`"
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

# Libnotify configuration
if test x$with_libnotify != xno; then
    PKG_CHECK_MODULES(NOTIFY, [ libnotify >= 0.7.0 ], [notifyver=7],
      [PKG_CHECK_MODULES(NOTIFY, [ libnotify >= 0.4.0 ], [notifyver=4],
        [AC_MSG_ERROR([*** You enabled notify but the library is not found.])])])
    AC_DEFINE_UNQUOTED(HAVE_NOTIFY,$notifyver,
                       [Defined when libnotify can be used.])
    BALSA_LIBS="$BALSA_LIBS $NOTIFY_LIBS"
    BALSA_AB_LIBS="$BALSA_AB_LIBS $NOTIFY_LIBS"
    with_libnotify=" >= 0.$notifyver"
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
    PKG_CHECK_MODULES(GTKSOURCEVIEW, [ gtksourceview-3.0 >= 3.2.0 ])
    AC_DEFINE(HAVE_GTKSOURCEVIEW,1,[Defined when GtkSourceView-3 can be used.])
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

# ige-mac-integration (Mac OS X desktop only)
AC_MSG_CHECKING([whether to include Mac OS X Desktop Integration])
if test x$use_igemacint != xno; then
    AC_MSG_CHECKING([for ige-mac-integration >= 0.8])
    if $PKG_CONFIG --atleast-version=0.8 ige-mac-integration ; then
	AC_MSG_RESULT([yes])
	AC_DEFINE(HAVE_MACOSX_DESKTOP,1,[Defined when menus can be integrated into the Mac OS X desktop])
	BALSA_CFLAGS="$BALSA_CFLAGS `$PKG_CONFIG --cflags ige-mac-integration`"
	BALSA_LIBS="$BALSA_LIBS `$PKG_CONFIG --libs ige-mac-integration`"
	BALSA_AB_LIBS="$BALSA_AB_LIBS `$PKG_CONFIG --libs ige-mac-integration`"
    else
	AC_MSG_ERROR([no])
    fi
else
    AC_MSG_RESULT([no])
fi

# Libsecret configuration
#
AC_MSG_CHECKING([whether to configure for libsecret])
if test x$with_libsecret != xno; then
    AC_MSG_RESULT([yes])
    PKG_CHECK_MODULES(LIBSECRET, [ libsecret-1 ], [],
                      AC_MSG_ERROR([[You asked to link to libsecret, but
                                    the development tool is not installed]]))
    AC_DEFINE(HAVE_LIBSECRET,1,[Defined when libsecret can be used.])
    BALSA_CFLAGS="$BALSA_CFLAGS $LIBSECRET_CFLAGS"
    BALSA_LIBS="$BALSA_LIBS $LIBSECRET_LIBS"
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
    PKG_CHECK_MODULES(SPELL, [ gspell-1 ])
    AC_DEFINE(HAVE_GSPELL,1,[Defined when Gspell can be used.])
    ;;
    gtkspell)
    if $PKG_CONFIG --atleast-version=3.0.3 gtkspell3-3.0; then
        AC_MSG_RESULT([gtkspell version >= 3.0.3])
        PKG_CHECK_MODULES(SPELL, [ gtkspell3-3.0 ])
        AC_DEFINE(HAVE_GTKSPELL_3_0_3,1,[Defined when GtkSpell version
                                         is at least 3.0.3.])
    else
        AC_MSG_RESULT([gtkspell version < 3.0.3])
        PKG_CHECK_MODULES(SPELL, [ gtkspell3-3.0 enchant ])
    fi
    AC_DEFINE(HAVE_GTKSPELL,1,[Defined when GtkSpell can be used.])
    ;;
    no)
    AC_MSG_RESULT([configure enchant])
    PKG_CHECK_MODULES(SPELL, [ enchant ])
    ;;
    *)
    AC_MSG_ERROR([bad option $use_spell_checker])
    ;;
esac
BALSA_CFLAGS="$BALSA_CFLAGS $SPELL_CFLAGS"
BALSA_LIBS="$BALSA_LIBS $SPELL_LIBS"
AM_CONDITIONAL([BUILD_WITH_SPELL_CHECKER], [test $use_spell_checker != "no"])

dnl #####################################################################
dnl 6. Typedefs, structures and compiler characteristics.
dnl #####################################################################
AC_C_CONST
AC_STDC_HEADERS

AC_CHECK_DECLS([ctime_r], [], [], [[#include <time.h>]])
AC_CHECK_FUNCS([ctime_r])
AC_CHECK_HEADER([zlib.h],,AC_MSG_ERROR([zlib library required]))

# more warnings.
#

if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
	for option in -Wsign-compare -Wpointer-arith -Wnested-externs \
            -Wchar-subscripts -Wuninitialized -Wmissing-prototypes    \
            -Wreturn-type -Wdeclaration-after-statement ; do
                SAVE_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_TRY_COMPILE([], [],
                        has_option=yes,
                        has_option=no,)
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
-DG_DISABLE_DEPRECATED \
-DG_DISABLE_SINGLE_INCLUDES \
-DGDK_DISABLE_DEPRECATED \
-DGDK_PIXBUF_DISABLE_DEPRECATED \
-DGDK_MULTIDEVICE_SAFE \
-DGMIME_DISABLE_DEPRECATED \
-DGNOME_DISABLE_DEPRECATED \
-DGNOME_DISABLE_DEPRECATED_SOURCE \
-DGNOME_VFS_DISABLE_DEPRECATED \
-DGTK_DISABLE_DEPRECATED \
-DGTK_DISABLE_SINGLE_INCLUDES \
-DNM_DISABLE_DEPRECATED \
-DPANGO_DISABLE_DEPRECATED \
-D_FORTIFY_SOURCE=2 \
"

    if test x"$use_html_widget" != xgtkhtml2 ; then
        BALSA_DEFS="$BALSA_DEFS \
        -DGDK_PIXBUF_DISABLE_SINGLE_INCLUDES"
    fi
else
	BALSA_FROM_GIT=no
fi

AC_MSG_CHECKING([for GIT information])
AC_MSG_RESULT($BALSA_FROM_GIT)

dnl #####################################################################
dnl 7. extra functions.
dnl #####################################################################
AC_CHECK_FUNCS(getaddrinfo)


dnl ###########################################################################
dnl Remember our prefixes
dnl   The way prefix et al are defined makes us jump through some hoops.
dnl ###########################################################################

if test "x$prefix" = "xNONE"; then
	prefix=$ac_default_prefix
fi
dnl datadir='${prefix}/share', so we must eval it or something. This works
BALSA_STD_PREFIX=`eval "echo ${prefix}"`
BALSA_DATA_PREFIX=`eval "echo ${datadir}/balsa"`
dnl autoconf-2.60 leaves a literal "${prefix}" in BALSA_DATA_PREFIX, so
dnl we clean it once more.
BALSA_DATA_PREFIX=`eval "echo ${BALSA_DATA_PREFIX}"`
AC_SUBST(BALSA_DATA_PREFIX)

dnl Consolidate them!
AC_PROG_AWK
BALSA_COMMON_PREFIXES=`sort<<EOF|uniq|${AWK} '{if(l!~/^$/)print l ",";l=$0}END{print l}'
"${BALSA_STD_PREFIX}"
"${BALSA_DATA_PREFIX}"
EOF
`

dnl Better way to do this?
AC_DEFINE_UNQUOTED( BALSA_STD_PREFIX, "$BALSA_STD_PREFIX",
                   [Directory for all balsa files.] )
AC_DEFINE_UNQUOTED( BALSA_DATA_PREFIX, "$BALSA_DATA_PREFIX",
	           [Directory for storing balsa data files.] )
AC_DEFINE_UNQUOTED( BALSA_COMMON_PREFIXES, `echo $BALSA_COMMON_PREFIXES`,
	           [Common directories for storing balsa icons.] ) 
dnl bleah


dnl ###########################################################################
dnl determine the base folder for installing the extra icons
dnl ###########################################################################

AC_ARG_ENABLE(extra-mimeicons,
        AC_HELP_STRING([--enable-extra-mimeicons],
                       [Install extra MIME icons (needs write access to the gtk+ icon folder, default=yes)]),[
	install_mimeicons=$enableval
],[
	install_mimeicons=yes
])
AM_CONDITIONAL([INSTALL_MIME_ICONS], [test $install_mimeicons = "yes"])
HICOLOR_ICON_DIR=`$PKG_CONFIG --variable=prefix gtk+-3.0`/share/icons/hicolor
AC_SUBST(HICOLOR_ICON_DIR)
dnl however, we do not mess with the icon caches - it messes up more
dnl things that it fixes, for eg. user installs, package builds etc.
dnl AC_PATH_PROG(GTK_UPDATE_ICON_CACHE, gtk-update-icon-cache)

dnl #####################################################################
dnl 8. Output
dnl #####################################################################

AC_OUTPUT([
Makefile
balsa.1
balsa.spec
balsa.desktop.in
balsa-mailto-handler.desktop.in
po/Makefile.in
sounds/Makefile
images/Makefile
images/mimetypes/Makefile
images/16x16/Makefile
images/24x24/Makefile
doc/Makefile
libbalsa/Makefile
libbalsa/imap/Makefile
libinit_balsa/Makefile
src/Makefile
ui/Makefile
])

dnl ##########################################################################
dnl Summary
dnl ##########################################################################

echo ""
echo "================ Final configuration ==================="
echo "    Installing into prefix: $prefix"
echo "      Using multithreading: $use_threads"
echo "   Enable compile warnings: $set_more_warnings"
echo "                Use ESMTP : $with_esmtp"
echo "               HTML widget: $use_html_widget"
echo "                 Use GNOME: $with_gnome"
echo "              Use Canberra: $with_canberra"
echo "                 Use GPGME: $gpgmecfg"
echo "                  Use LDAP: $with_ldap"
echo "                   Use GSS: $with_gss"
echo "                Use SQLite: $with_sqlite"
echo "    External spell checker: $use_spell_checker"
echo "             Use Libnotify: $with_libnotify"
echo "         Use GtkSourceView: $with_gtksourceview"
echo "              Use Compface: $with_compface"
echo "  Install extra MIME icons: $install_mimeicons"
echo "             Use libsecret: $with_libsecret"
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
