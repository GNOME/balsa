/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#include "libbalsa.h"
#include "mailbackend.h"

void mutt_message (const char *fmt,...);
void mutt_exit (int code);
int mutt_yesorno (const char *msg, int def);
int mutt_any_key_to_continue (const char *s);
void mutt_clear_error (void);

void
mutt_message (const char *fmt,...)
{
#ifdef DEBUG
  va_list ap;
  char outstr[522];

  va_start (ap, fmt);
  vsprintf (outstr, fmt, ap);
  va_end (ap);
  g_print ("mutt_message: %s\n", outstr);
#endif
}

void
mutt_exit (int code)
{
}

int
mutt_yesorno (const char *msg, int def)
{
  return 1;
}

/*  int */
/*  mutt_any_key_to_continue (const char *s) */
/*  { */
/*    return 1; */
/*  } */

void
mutt_clear_error (void)
{
}

/* We're gonna set Mutt global vars here */
void
libbalsa_init ( void (*error_func) (const char *fmt,...) )
{
  struct utsname utsname;
  char *p;
  gchar *tmp;

  Spoolfile = libbalsa_guess_mail_spool();

  uname (&utsname);

  Username = g_get_user_name ();

  Homedir = g_get_home_dir ();

  Realname = g_get_real_name ();

  Hostname = libbalsa_get_hostname ();

  mutt_error = error_func;

  Fqdn = g_strdup (Hostname);

  Sendmail = SENDMAIL;

  Shell = g_strdup ((p = g_getenv ("SHELL")) ? p : "/bin/sh");
  Tempdir = g_get_tmp_dir ();

  if (UserHeader)
    UserHeader = UserHeader->next;
  UserHeader = mutt_new_list ();
  tmp = g_malloc (17 + strlen (VERSION));
  snprintf (tmp, 17 + strlen (VERSION), "X-Mailer: Balsa %s", VERSION);
  UserHeader->data = g_strdup (tmp);
  g_free (tmp);
  
  set_option(OPTSAVEEMPTY);
  set_option (OPTCHECKNEW);
}

void
libbalsa_set_spool (gchar *spool)
{
  if ( Spoolfile )
    g_free(Spoolfile);

  if ( spool ) 
    Spoolfile = g_strdup (spool);
  else 
    Spoolfile = libbalsa_guess_mail_spool();
}

/* libbalsa_guess_mail_spool

   Returns an allocated gchar * with our best guess of the user's
   mail spool file.
*/
gchar*
libbalsa_guess_mail_spool( void )
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
  
  if( (env = getenv( "MAIL" )) != NULL )
    return g_strdup( env );
  
  if( (env = getenv( "USER" )) != NULL ) {
    for( i = 0; guesses[i] != NULL; i++ ) {
      spool = g_strconcat( guesses[i], env, NULL );
      
      if( g_file_exists( spool ) )
	return spool;
      
      g_free( spool );
    }
  }
  
  /* libmutt's configure.in indicates that this 
   * ($HOME/mailbox) exists on
   * some systems, and it's a good enough default if we
   * can't guess it any other way. */
  return gnome_util_prepend_user_home( "mailbox" );
}
