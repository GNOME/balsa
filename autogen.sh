#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="Gnome Balsa"

(test -f $srcdir/configure.in \
  && test -d $srcdir/libmutt \
  && test -d $srcdir/src) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level balsa directory"

    exit 1
}

# GNOME´s autogen.sh does not pass --intl option to gettextize,
# let's call gettextize ourselves
gettextize --force --copy --intl

# call GNOME´s autogen.sh.
. $srcdir/macros/autogen.sh
