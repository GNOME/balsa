
/* Enable debugging info */
#define DEBUG

/* Disable the X-Mailer header? */
#undef NO_XMAILER

/* What is your domain name? */
#undef DOMAIN

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

/* Do you want support for IMAP GSSAPI authentication? (--with-gss) */
#undef USE_GSS

/* Do you have the Heimdal version of Kerberos V? (for gss support) */
#undef HAVE_HEIMDAL

/* Do you want support for SSL? (--enable-ssl) */
#undef USE_SSL

/* Avoid SSL routines which used patent-encumbered RC5 algorithms */
#undef NO_RC5

/* Avoid SSL routines which used patent-encumbered IDEA algorithms */
#undef NO_IDEA

/* Avoid SSL routines which used patent-encumbered RSA algorithms */
#undef NO_RSA

/*
 * Is mail spooled to the user's home directory?  If defined, MAILPATH should
 * be set to the filename of the spool mailbox relative the the home
 * directory.
 * use: configure --with-homespool=FILE
 */
#undef HOMESPOOL

/* Where new mail is spooled */
#undef MAILPATH

/* Does your system have the srand48() suite? */
#undef HAVE_SRAND48

/* Where to find sendmail on your system */
#undef SENDMAIL

/* Where to find ispell on your system? */
#undef ISPELL

/* Should Mutt run setgid "mail" ? */
#undef USE_SETGID

/* Are we using GNU rx? */
#undef USE_GNU_RX

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

/* Does your system have the fchdir() call? */
#undef HAVE_FCHDIR
