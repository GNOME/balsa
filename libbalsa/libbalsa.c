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

#include <string.h>
#include <gnome.h>

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

int
mutt_any_key_to_continue (const char *s)
{
  return 1;
}

void
mutt_clear_error (void)
{
}

