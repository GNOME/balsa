/*
 * Copyright (C) 1998 Michael R. Elkins <me@cs.hmc.edu>
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
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */ 

#include "mutt.h"
#include "globals.h"
#include "mutt_socket.h"

#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

/* support for multiple socket connections */

static CONNECTION *Connections = NULL;
static int NumConnections = 0;

/* simple read buffering to speed things up. */
int mutt_socket_readchar (CONNECTION *conn, char *c)
{
  if (conn->bufpos >= conn->available)
  {
    conn->available = read (conn->fd, conn->inbuf, LONG_STRING);
    dprint (1, (debugfile, "mutt_socket_readchar(): buffered %d chars\n", conn->available));
    conn->bufpos = 0;
    if (conn->available <= 0)
      return conn->available; /* returns 0 for EOF or -1 for other error */
  }
  *c = conn->inbuf[conn->bufpos];
  conn->bufpos++;
  return 1;
}

int mutt_socket_read_line (char *buf, size_t buflen, CONNECTION *conn)
{
  char ch;
  int i;

  for (i = 0; i < buflen; i++)
  {
    if (mutt_socket_readchar (conn, &ch) != 1)
      return (-1);
    if (ch == '\n')
      break;
    buf[i] = ch;
  }
  buf[i-1] = 0;
  return (i + 1);
}

int mutt_socket_read_line_d (char *buf, size_t buflen, CONNECTION *conn)
{
  int r = mutt_socket_read_line (buf, buflen, conn);
  dprint (1,(debugfile,"mutt_socket_read_line_d():%s\n", buf));
  return r;
}

int mutt_socket_write (CONNECTION *conn, const char *buf)
{
  dprint (1,(debugfile,"mutt_socket_write():%s", buf));
  return (write (conn->fd, buf, strlen (buf)));
}

CONNECTION *mutt_socket_select_connection (char *host, int port, int flags)
{
  int x;

  if (flags != M_NEW_SOCKET)
  {
    for (x = 0; x < NumConnections; x++)
    {
      if (!strcmp (host, Connections[x].server) && 
	  (port == Connections[x].port))
	return &Connections[x];
    }
  }
  if (NumConnections == 0)
  {
    NumConnections = 1;
    Connections = (CONNECTION *) safe_malloc (sizeof (CONNECTION));
  }
  else
  {
    NumConnections++;
    safe_realloc ((void *)&Connections, sizeof (CONNECTION) * NumConnections);
  }
  Connections[NumConnections - 1].bufpos = 0;
  Connections[NumConnections - 1].available = 0;
  Connections[NumConnections - 1].uses = 0;
  Connections[NumConnections - 1].server = safe_strdup (host);
  Connections[NumConnections - 1].port = port;

  return &Connections[NumConnections - 1];
}

