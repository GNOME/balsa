/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter and Jay Painter
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

#include "libbalsa.h"

/*
 * body
 */
LibBalsaMessageBody *
libbalsa_message_body_new ()
{
  LibBalsaMessageBody *body;

  body = g_new (LibBalsaMessageBody, 1);
  body->htmlized = NULL;
  body->buffer = NULL;
  body->mutt_body = NULL;
  body->filename = NULL;
  body->charset  = NULL;
  return body;
}


void
libbalsa_message_body_free (LibBalsaMessageBody * body)
{
  if (!body)
    return;

  g_free (body->htmlized);
  g_free (body->buffer);
  g_free (body->filename);
  g_free (body->charset);
  g_free (body->buffer);
  g_free (body);
}

