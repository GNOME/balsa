#! /bin/sh
# bootstrap file to be used when autogen.sh fails.
test -n "$srcdir" || srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

olddir=`pwd`

cd $srcdir

echo "Running gettextize...  Ignore non-fatal messages."
gettextize --force --no-changelog || exit 1
echo "Running libtoolize..."
libtoolize --force || exit 1
echo "Running aclocal..."
aclocal || exit 1
echo "Running autoconf..."
autoconf || exit 1
echo "Running autoheader..."
autoheader || exit 1
echo "Running automake..."
automake --gnu --add-missing --copy || exit 1

cd $olddir

echo "Running configure $* ..."
exec $srcdir/configure "$@"
