/* config.h.  Generated automatically by configure.  */
/* config.h.in.  Generated automatically from configure.in by autoheader.  */

/* Enable debugging info */
#define DEBUG

/* Does your version of PGP support the PGPPASSFD environment variable? */
#define HAVE_PGPPASSFD

/* Disable the X-Mailer header? */
/* #undef NO_XMAILER */

/* What is your domain name? */
/* #undef DOMAIN */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef pid_t */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define if `sys_siglist' is declared by <signal.h>.  */
#define SYS_SIGLIST_DECLARED 1

/* Mutt version info */
#define VERSION "0.93"

/* use dotlocking to lock mailboxes? */
#undef USE_DOTLOCK 1

/* use flock() to lock mailboxes? */
/* #undef USE_FLOCK */

/* Use fcntl() to lock folders? */
#define USE_FCNTL 1

/*
 * Define if you have problems with mutt not detecting new/old mailboxes
 * over NFS.  Some NFS implementations incorrectly cache the attributes
 * of small files.
 */
/* #undef NFS_ATTRIBUTE_HACK */

/* Do you want support for the POP3 protocol? (--enable-pop) */
/* #undef USE_POP */

/* Do you want support for the IMAP protocol? (--enable-imap) */
/* #undef USE_IMAP */

/*
 * Is mail spooled to the user's home directory?  If defined, MAILPATH should
 * be set to the filename of the spool mailbox relative the the home
 * directory.
 * use: configure --with-homespool=FILE
 */
/* #undef HOMESPOOL */

/* Where new mail is spooled */
#define MAILPATH "/var/spool/mail"

/* Should I just use the domain name? (--enable-hidden-host) */
/* #undef HIDDEN_HOST */

/* Where to find sendmail on your system */
#define SENDMAIL "/usr/sbin/sendmail"

/* Where is PGP located on your system? */
/* #undef _PGPPATH */

/* Where is PGP 2.* located on your system? */
/* #undef _PGPV2PATH */

/* Where is PGP 5 located on your system? */
/* #undef _PGPV3PATH */

/* Where is GNU Privacy Guard located on your system? */
/* #undef _PGPGPGPATH */

/* Do we have PGP 2.*? */
/* #undef HAVE_PGP2 */

/* Do we have PGP 5.0 or up? */
/* #undef HAVE_PGP5 */

/* Do we have GPG? */
/* #undef HAVE_GPG */

/* Where to find ispell on your system? */
#define ISPELL "/usr/bin/ispell"

/* Should Mutt run setgid "mail" ? */
#define USE_SETGID 1

/* Does your curses library support color? */
#define HAVE_COLOR 1

/* Are we using GNU rx? */
/* #undef USE_GNU_RX */

/* Compiling with SLang instead of curses/ncurses? */
/* #undef USE_SLANG_CURSES */

/* program to use for shell commands */
#define EXECSHELL "/bin/sh"

/* The "buffy_size" feature */
/* #undef BUFFY_SIZE */

/* The result of isprint() is unreliable? */
/* #undef LOCALES_HACK */

/* Enable exact regeneration of email addresses as parsed?  NOTE: this requires
   significant more memory when defined. */
/* #undef EXACT_ADDRESS */

/* Does your system have the snprintf() call? */
#define HAVE_SNPRINTF 1

/* Does your system have the vsnprintf() call? */
#define HAVE_VSNPRINTF 1

/* The number of bytes in a long.  */
#define SIZEOF_LONG 4

/* Define if you have the bkgdset function.  */
#define HAVE_BKGDSET 1

/* Define if you have the curs_set function.  */
#define HAVE_CURS_SET 1

/* Define if you have the ftruncate function.  */
#define HAVE_FTRUNCATE 1

/* Define if you have the meta function.  */
#define HAVE_META 1

/* Define if you have the regcomp function.  */
#define HAVE_REGCOMP 1

/* Define if you have the resizeterm function.  */
#define HAVE_RESIZETERM 1

/* Define if you have the setegid function.  */
#define HAVE_SETEGID 1

/* Define if you have the srand48 function.  */
#define HAVE_SRAND48 1

/* Define if you have the strcasecmp function.  */
#define HAVE_STRCASECMP 1

/* Define if you have the strerror function.  */
#define HAVE_STRERROR 1

/* Define if you have the strftime function.  */
#define HAVE_STRFTIME 1

/* Define if you have the typeahead function.  */
#define HAVE_TYPEAHEAD 1

/* Define if you have the use_default_colors function.  */
/* #undef HAVE_USE_DEFAULT_COLORS */

/* Define if you have the <ncurses.h> header file.  */
#define HAVE_NCURSES_H 1

/* Define if you have the <stdarg.h> header file.  */
#define HAVE_STDARG_H 1

/* Define if you have the <sys/ioctl.h> header file.  */
#define HAVE_SYS_IOCTL_H 1

/* Define if you have the intl library (-lintl).  */
/* #undef HAVE_LIBINTL */

/* Define if you have the nsl library (-lnsl).  */
/* #undef HAVE_LIBNSL */

/* Define if you have the socket library (-lsocket).  */
/* #undef HAVE_LIBSOCKET */

/* Define if you have the x library (-lx).  */
/* #undef HAVE_LIBX */
