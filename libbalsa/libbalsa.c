/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#include "libbalsa.h"
#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
static GMutex *mutt_lock;
#endif

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
    char *p;

    Spoolfile = libbalsa_guess_mail_spool();

#ifdef BALSA_USE_THREADS
    if (!g_thread_supported()) {
	g_error("Threads have not been initialised.");
    }
    mutt_lock = g_mutex_new();
#endif

    uname(&utsname);

    Username = g_get_user_name();

    Homedir = g_get_home_dir();

    Realname = g_get_real_name();

    Hostname = libbalsa_get_hostname();

    libbalsa_real_information_func = information_callback;

    mutt_error = libbalsa_mutt_error;

    Fqdn = g_strdup(Hostname);

    Sendmail = SENDMAIL;

    Shell = g_strdup((p = g_getenv("SHELL")) ? p : "/bin/sh");
    Tempdir = g_get_tmp_dir();

    if (UserHeader)
	UserHeader = UserHeader->next;

    UserHeader = mutt_new_list();
    UserHeader->data = g_strdup_printf("X-Mailer: Balsa %s", VERSION);

    set_option(OPTSAVEEMPTY);
    set_option(OPTCHECKNEW);

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

/*
 * This function is hooked into the mutt_error callback
 *
 */
static void
libbalsa_mutt_error(const char *fmt, ...)
{
    va_list va_args;

    va_start(va_args, fmt);
    libbalsa_information_varg(LIBBALSA_INFORMATION_WARNING, fmt, va_args);
    va_end(va_args);
}
