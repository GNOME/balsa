#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="Balsa"
REQUIRED_AUTOMAKE_VERSION=1.7

(test -f $srcdir/configure.in \
  && test -d $srcdir/src \
  && test -f $srcdir/src/balsa-app.h) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level balsa directory"
    exit 1
}

if libtool --version >/dev/null 2>&1; then
    vers=`libtool --version | sed -e "s/^[^0-9]*//" | awk '{ split($1,a,"\."); print (a[1]*1000+a[2])*1000+a[3];exit 0;}'`
    if test "$vers" -ge 1003003; then
        true
    else
        echo "Please upgrade your libtool to version 1.3.3 or better." 1>&2
        exit 1
    fi
fi

ifs_save="$IFS"; IFS=":"
for dir in $PATH ; do
  test -z "$dir" && dir=.
  if test -f $dir/gnome-autogen.sh ; then
    gnome_autogen="$dir/gnome-autogen.sh"
    gnome_datadir=`echo $dir | sed -e 's,/bin$,/share,'`
    break
  fi
done
IFS="$ifs_save"

if test -n "$gnome_autogen" ; then
  GNOME_DATADIR="$gnome_datadir" USE_GNOME2_MACROS=1 . $gnome_autogen
  exit 0
fi

echo "gnome-autogen.sh not found."
echo "Assuming you have a 'native' install of GNOME2."

# GNOME2 is properly installed on the system.
# Do the things the usual way.
# 
glib-gettextize -f -c

# call GNOME's autogen.sh.
. $srcdir/macros/autogen.sh
