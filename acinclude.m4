AC_DEFUN([AM_HDR_SIGACTION],
[AC_CACHE_CHECK([for sigaction.h], am_cv_hdr_sigaction,
[AC_TRY_LINK([#include <sigaction.h>],
              [int i = 0;],
              am_cv_hdr_sigaction=yes,
              am_cv_hdr_sigaction=no)])
if test $am_cv_hdr_sigaction = yes; then
   AC_DEFINE(HAVE_SIGACTION_H)
fi
])
              
AC_DEFUN([AM_HDR_SIGSET],
[AC_CACHE_CHECK([for sigset.h], am_cv_hdr_sigset,
[AC_TRY_LINK([#include <sigset.h>],
              [int i = 0;],
              am_cv_hdr_sigset=yes,
              am_cv_hdr_sigset=no)])
if test $am_cv_hdr_sigset = yes; then
   AC_DEFINE(HAVE_SIGSET_H)
fi
])
