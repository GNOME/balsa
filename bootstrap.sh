#! /bin/sh
# bootstrap file to be used when autogen.sh fails.
echo "Running gettextize...  Ignore non-fatal messages."
glib-gettextize --force --copy || exit 1
echo "running intltoolize..."
intltoolize --copy --force --automake
echo "Running aclocal..."
aclocal || exit 1
echo "Running autoheader..."
autoheader || exit 1
echo "Running libtoolize..."
libtoolize --force || exit 1
echo "Running automake..."
automake-1.7 --foreign --add-missing --copy
echo "Running autoconf..."
autoconf || exit 1
echo "Running configure $* ..."
./configure "$@"
