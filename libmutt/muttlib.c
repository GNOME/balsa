/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>
 * Copyright (C) 1999-2000 Thomas Roessler <roessler@guug.de>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

#include "mutt.h"

void mutt_buffer_addstr (BUFFER* buf, const char* s)
{
  mutt_buffer_add (buf, s, mutt_strlen (s));
}

void mutt_buffer_addch (BUFFER* buf, char c)
{
  mutt_buffer_add (buf, &c, 1);
}

/* dynamically grows a BUFFER to accomodate s, in increments of 128 bytes.
 * Always one byte bigger than necessary for the null terminator, and
 * the buffer is always null-terminated */
void mutt_buffer_add (BUFFER* buf, const char* s, size_t len)
{
  size_t offset;

  if (buf->dptr + len + 1 > buf->data + buf->dsize)
  {
    offset = buf->dptr - buf->data;
    buf->dsize += len < 128 ? 128 : len + 1;
    safe_realloc ((void**) &buf->data, buf->dsize);
    buf->dptr = buf->data + offset;
  }
  memcpy (buf->dptr, s, len);
  buf->dptr += len;
  *(buf->dptr) = '\0';
}
