
/* Enable debugging info */
#define DEBUG

/* Does your version of PGP support the PGPPASSFD environment variable? */
#define HAVE_PGPPASSFD

/* Disable the X-Mailer header? */
#undef NO_XMAILER

/* What is your domain name? */
#undef DOMAIN
@TOP@

/* Mutt version info */
#undef VERSION

/* use dotlocking to lock mailboxes? */
#undef USE_DOTLOCK

/* use flock() to lock mailboxes? */
#undef USE_FLOCK

/* Use fcntl() to lock folders? */
#undef USE_FCNTL

/*
 * Define if you have problems with mutt not detecting new/old mailboxes
 * over NFS.  Some NFS implementations incorrectly cache the attributes
 * of small files.
 */
#undef NFS_ATTRIBUTE_HACK

/* Do you want support for the POP3 protocol? (--enable-pop) */
#undef USE_POP

/* Do you want support for the IMAP protocol? (--enable-imap) */
#undef USE_IMAP

/*
 * Is mail spooled to the user's home directory?  If defined, MAILPATH should
 * be set to the filename of the spool mailbox relative the the home
 * directory.
 * use: configure --with-homespool=FILE
 */
#undef HOMESPOOL

/* Where new mail is spooled */
#undef MAILPATH

/* Should I just use the domain name? (--enable-hidden-host) */
#undef HIDDEN_HOST

/* Does your system have the srand48() suite? */
#undef HAVE_SRAND48

/* Where to find sendmail on your system */
#undef SENDMAIL

/* Where is PGP located on your system? */
#undef _PGPPATH

/* Where is PGP 2.* located on your system? */
#undef _PGPV2PATH

/* Where is PGP 5 located on your system? */
#undef _PGPV3PATH

/* Where is GNU Privacy Guard located on your system? */
#undef _PGPGPGPATH

/* Do we have PGP 2.*? */
#undef HAVE_PGP2

/* Do we have PGP 5.0 or up? */
#undef HAVE_PGP5

/* Do we have GPG? */
#undef HAVE_GPG

/* Where to find ispell on your system? */
#undef ISPELL

/* Should Mutt run setgid "mail" ? */
#undef USE_SETGID

/* Does your curses library support color? */
#undef HAVE_COLOR

/* Are we using GNU rx? */
#undef USE_GNU_RX

/* Compiling with SLang instead of curses/ncurses? */
#undef USE_SLANG_CURSES

/* program to use for shell commands */
#define EXECSHELL "/bin/sh"

/* The "buffy_size" feature */
#undef BUFFY_SIZE

/* The result of isprint() is unreliable? */
#undef LOCALES_HACK

/* Enable exact regeneration of email addresses as parsed?  NOTE: this requires
   significant more memory when defined. */
#undef EXACT_ADDRESS

/* Does your system have the snprintf() call? */
#undef HAVE_SNPRINTF

/* Does your system have the vsnprintf() call? */
#undef HAVE_VSNPRINTF
