#!/bin/sh

xgettext --default-domain=balsa --directory=.. \
  --add-comments --keyword=_ --keyword=N_ \
  --files-from=./POTFILES.in \
&& test ! -f balsa.po \
   || ( rm -f ./balsa.pot \
    && mv balsa.po ./balsa.pot )
