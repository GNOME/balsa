#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="Balsa"

(test -f $srcdir/configure.in \
  && test -d $srcdir/src \
  && test -f $srcdir/src/balsa-app.h) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level balsa directory"
    exit 1
}

if libtool --version >/dev/null 2>&1; then
    vers=`libtool --version | sed -e "s/^[^0-9]*//" -e "s/ .*$//" | awk 'BEGIN { FS = "."; } { printf "%d", ($1 * 1000 + $2) * 1000 + $3;}'`
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

# GNOME's autogen.sh does not pass --intl option to gettextize, and
# gnome-autogen.sh does not need that.
# this definition is an ugly hack until autobuild tools stabilize.
function gettextize {
  if test -d libmutt; then
     `which gettextize` --intl $*;
  else
     `which gettextize` $*;
  fi
}

if test -n "$gnome_autogen" ; then
  GNOME_DATADIR="$gnome_datadir" USE_GNOME2_MACROS=1 . $gnome_autogen
  exit 0
fi

echo "gnome-autogen.sh not found."
echo "Assuming you have a 'native' install of GNOME2."
sleep 3

# GNOME2 is properly installed on the system.
# Do the things the usual way.

# call GNOME's autogen.sh.
. $srcdir/macros/autogen.sh
