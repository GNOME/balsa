/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>
#include <gnome.h>

#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <stdarg.h>

#if ENABLE_LDAP
#include <ldap.h>
#endif

#include "libbalsa.h"
#include "misc.h"
#include "mailbackend.h"


#ifdef BALSA_USE_THREADS
static GMutex *mutt_lock;
#endif

#define POP_SERVER "pop"
#define IMAP_SERVER "mx"
#define LDAP_SERVER "ldap"

static gchar *Domainname;

void mutt_message(const char *fmt, ...);
void mutt_exit(int code);
int mutt_yesorno(const char *msg, int def);
int mutt_any_key_to_continue(const char *s);
void mutt_clear_error(void);

static void libbalsa_mutt_error(const char *fmt, ...);

void
mutt_message(const char *fmt, ...)
{
    va_list va_args;

    va_start(va_args, fmt);
    libbalsa_information_varg(LIBBALSA_INFORMATION_MESSAGE, fmt, va_args);
    va_end(va_args);
}

void
mutt_exit(int code)
{
}

int
mutt_yesorno(const char *msg, int def)
{
    libbalsa_information(LIBBALSA_INFORMATION_DEBUG, "YES/NO: %s (%d)",
			 msg, def);
    return 1;
}

void
mutt_clear_error(void)
{
}

/* We're gonna set Mutt global vars here */
void
libbalsa_init(LibBalsaInformationFunc information_callback)
{
    struct utsname utsname;
    const char *p;

    Spoolfile = libbalsa_guess_mail_spool();

#ifdef BALSA_USE_THREADS
    if (!g_thread_supported()) {
	g_error("Threads have not been initialised.");
    }
    mutt_lock = g_mutex_new();
#endif

    uname(&utsname);

    /* Username, Homedir etc. are really const char* */
    Username   = (char*)g_get_user_name();
    Homedir    = (char*)g_get_home_dir();
    Realname   = (char*)g_get_real_name();
    Hostname   = (char*)libbalsa_get_hostname();
    Domainname = (char*)libbalsa_get_domainname();

    libbalsa_real_information_func = information_callback;

    mutt_error = libbalsa_mutt_error;

    if ( Domainname ) 
	Fqdn = g_strdup_printf("%s.%s", Hostname, Domainname);
    else
	Fqdn = g_strdup(Hostname);

    Sendmail = SENDMAIL;

    Shell   = g_strdup((p = g_getenv("SHELL")) ? p : "/bin/sh");
    Tempdir = (char*)g_get_tmp_dir();

    if (UserHeader)
	UserHeader = UserHeader->next;

    UserHeader = mutt_new_list();
    UserHeader->data = g_strdup_printf("X-Mailer: Balsa %s", VERSION);

    set_option(OPTSAVEEMPTY);
    set_option(OPTCHECKNEW);
    set_option(OPTMHPURGE);
#ifdef USE_SSL
    set_option(OPTSSLSYSTEMCERTS);
#endif /* USE_SSL */

    FileMask.rx = (regex_t *) safe_malloc (sizeof (regex_t));
    REGCOMP(FileMask.rx,"!^\\.[^.]",0);
    ReplyRegexp.rx = (regex_t *) safe_malloc (sizeof (regex_t));
    REGCOMP(ReplyRegexp.rx,"^(re([\\[0-9\\]+])*|aw):[ \t]*",0);

    libbalsa_notify_init();

    /* Register our types */
    /* So that libbalsa_mailbox_new_from_config will work... */
    LIBBALSA_TYPE_MAILBOX_LOCAL;
    LIBBALSA_TYPE_MAILBOX_POP3;
    LIBBALSA_TYPE_MAILBOX_IMAP;
    LIBBALSA_TYPE_MAILBOX_MBOX;
    LIBBALSA_TYPE_MAILBOX_MH;
    LIBBALSA_TYPE_MAILBOX_MAILDIR;

    LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN;
    LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
#if ENABLE_LDAP
    LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
#endif
}

void
libbalsa_set_spool(gchar * spool)
{
    if (Spoolfile)
	g_free(Spoolfile);

    if (spool)
	Spoolfile = g_strdup(spool);
    else
	Spoolfile = libbalsa_guess_mail_spool();
}

/*
 * These two functions control the libmutt lock. 
 * This lock must be held around all mutt calls
 */
void
libbalsa_lock_mutt(void)
{
#ifdef BALSA_USE_THREADS
    g_mutex_lock(mutt_lock);
#endif
}

void
libbalsa_unlock_mutt(void)
{
#ifdef BALSA_USE_THREADS
    g_mutex_unlock(mutt_lock);
#endif
}


/* libbalsa_guess_email_address:
   Email address can be determined in four ways:
   1. Using the environment variable 'EMAIL'

   2. The file '/etc/mailname' should contain the external host
      address for the host. Prepend the username (`username`@`cat
      /etc/mailname`).

   3. Append the domainname to the user name.
   4. Append the hostname to the user name.

*/
gchar*
libbalsa_guess_email_address(void)
{
    /* Q: Find this location with configure? or at run-time? */
    static const gchar* MAILNAME_FILE = "/etc/mailname";
    char hostbuf[512];

    gchar* preset, *domain;
    if(g_getenv("EMAIL") != NULL){                  /* 1. */
        preset = g_strdup(g_getenv("EMAIL"));
    } else if(access(MAILNAME_FILE, F_OK) == 0){    /* 2. */
        FILE *mailname_in = fopen(MAILNAME_FILE, "r");
        fgets(hostbuf, 511, mailname_in);
        hostbuf[strlen(hostbuf)-1] = '\0';
        fclose(mailname_in);
        preset = g_strconcat(g_get_user_name(), "@", hostbuf, NULL);
        
    }else if((domain = libbalsa_get_domainname())){ /* 3. */
        preset = g_strconcat(g_get_user_name(), "@", domain, NULL);
        g_free(domain);    
    } else {                                        /* 4. */
        gethostname(hostbuf, 511);
        preset = g_strconcat(g_get_user_name(), "@", hostbuf, NULL);
    }
    return preset;
}

/* libbalsa_guess_mail_spool

   Returns an allocated gchar * with our best guess of the user's
   mail spool file.
*/
gchar *
libbalsa_guess_mail_spool(void)
{
    int i;
    gchar *env;
    gchar *spool;
    static const gchar *guesses[] = {
	"/var/spool/mail/",
	"/var/mail/",
	"/usr/spool/mail/",
	"/usr/mail/",
	NULL
    };

    if ((env = getenv("MAIL")) != NULL)
	return g_strdup(env);

    if ((env = getenv("USER")) != NULL) {
	for (i = 0; guesses[i] != NULL; i++) {
	    spool = g_strconcat(guesses[i], env, NULL);

	    if (g_file_exists(spool))
		return spool;

	    g_free(spool);
	}
    }

    /* libmutt's configure.in indicates that this 
     * ($HOME/mailbox) exists on
     * some systems, and it's a good enough default if we
     * can't guess it any other way. */
    return gnome_util_prepend_user_home("mailbox");
}

/* Some more "guess" functions symmetric to libbalsa_guess_mail_spool()... */


static gchar *qualified_hostname(const char *name)
{
    gchar *domain=libbalsa_get_domainname();

    if(domain) {
	gchar *host=g_strdup_printf("%s.%s", name, domain);
	
	g_free(domain);

	return host;
    } else
	return g_strdup(name);
}


gchar *libbalsa_guess_pop_server()
{
    return qualified_hostname(POP_SERVER);
}

gchar *libbalsa_guess_imap_server()
{
    return qualified_hostname(IMAP_SERVER);
}

gchar *libbalsa_guess_ldap_server()
{
    return qualified_hostname(LDAP_SERVER);
}

gchar *libbalsa_guess_imap_inbox()
{
    gchar *server = libbalsa_guess_imap_server();

    if(server) {
	gchar *url = g_strdup_printf("imap://%s/INBOX", server);
	
	g_free(server);

	return url;
    }

    return NULL;
}

gchar *libbalsa_guess_ldap_base()
{
    gchar *server = libbalsa_guess_ldap_server();

    /* Note: Assumes base dn is "o=<domain name>". Somewhat speculative... */
    if(server) {
	gchar *base=NULL, *domain;

	if((domain=strchr(server, '.')))
	   base = g_strdup_printf("o=%s", domain+1);
	
	g_free(server);

	return base;
    }
    return NULL;
}

gchar *libbalsa_guess_ldap_name()
{
    gchar *base = libbalsa_guess_ldap_base();

    if(base) {
	gchar *name = strchr(base, '=');
	gchar *dir_name = g_strdup_printf(_("LDAP Directory for %s"), 
					  (name?name+1:base));
	g_free(base);

	return dir_name;
    } 

    return NULL;
}

gchar *libbalsa_guess_ldif_file()
{
    int i;
    gchar *ldif;

    static const gchar *guesses[] = {
	"address.ldif",
	".address.ldif",
	"address-book.ldif",
	".address-book.ldif",
	NULL
    };

    for (i = 0; guesses[i] != NULL; i++) {
	ldif = gnome_util_prepend_user_home(guesses[i]);
	
	if (g_file_exists(ldif))
	     return ldif;
	  
	g_free(ldif);
    }
    return  gnome_util_prepend_user_home(guesses[0]); /* *** Or NULL */
    
}

gboolean libbalsa_ldap_exists(const gchar *server)
{
#if ENABLE_LDAP
    LDAP *ldap = ldap_open(server, LDAP_PORT);

    if(ldap) {
	ldap_unbind(ldap);

	return TRUE;
    }
#endif /* #if ENABLE_LDAP */

    return FALSE;
}


/*
 * This function is hooked into the mutt_error callback
 * mutt sometimes generates empty messages, ignore them.
 */
static void
libbalsa_mutt_error(const char *fmt, ...)
{
    va_list va_args;

    va_start(va_args, fmt);
    if(*fmt) 
	libbalsa_information_varg(LIBBALSA_INFORMATION_WARNING, fmt, va_args);
    va_end(va_args);
}
