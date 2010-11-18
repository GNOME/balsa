#! /bin/sh
# bootstrap file to be used when autogen.sh fails.
echo "Running gettextize...  Ignore non-fatal messages."
glib-gettextize --force --copy || exit 1
echo "running intltoolize..."
[ -d m4 ] || mkdir m4
intltoolize --copy --force --automake || exit 1
echo "Running gnome-doc-prepare --force - ignore errors."
if gnome-doc-prepare --force > /dev/null 2>&1; then
   :
else
    test -L gnome-doc-utils.make && rm gnome-doc-utils.make
    touch gnome-doc-utils.make
fi
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
echo "Running configure $* ..."
./configure "$@"
gnome-doc-tool -V > /dev/null 2>&1 || echo "gnome-doc-utils required to make dist"

