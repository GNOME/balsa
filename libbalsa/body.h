/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1999 Stuart Parmenter
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

#ifndef __LIBBALSA_BODY_H__
#define __LIBBALSA_BODY_H__

#include <glib.h>

#include "libmutt/mutt.h"

struct _LibBalsaMessageBody
{
  gchar *buffer;		/* holds raw data of the MIME part, or NULL */
  gchar *htmlized;		/* holds htmlrep of buffer, or NULL */
  BODY *mutt_body;		/* pointer to BODY struct of mutt message */
  gchar *filename;		/* holds filename for attachments and such (used mostly for sending) */
  gchar *charset;               /* the charset, used for sending, replying. */
};

LibBalsaMessageBody *libbalsa_message_body_new(void);
void libbalsa_message_body_free(LibBalsaMessageBody * body);

#endif /* __LIBBALSA_BODY_H__ */
