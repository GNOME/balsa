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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include <gnome.h>

#include "libbalsa.h"
#include "misc.h"

MailboxNode *
mailbox_node_new (const gchar * name, Mailbox * mb, gint i)
{
  MailboxNode *mbn;
  mbn = g_malloc (sizeof (MailboxNode));
  mbn->name = g_strdup (name);
  if (mb)
    mbn->mailbox = mb;
  else
    mbn->mailbox = NULL;
  mbn->IsDir = i;
  mbn->expanded = FALSE;
  mbn->style = 0;

  return mbn;
}

void mailbox_node_destroy(MailboxNode * mbn)
{
  g_return_if_fail(mbn != NULL);

  g_free(mbn->name);
  g_free(mbn);
}
gchar *
g_get_host_name (void)
{
  struct utsname utsname;
  uname (&utsname);
  return g_strdup (utsname.nodename);
}


gchar *
address_to_gchar (const Address * addr)
{
  gchar *retc = NULL;

  if (addr->personal) {
     if(addr->mailbox)
	retc= g_strdup_printf("%s <%s>", addr->personal, addr->mailbox);
     else retc = g_strdup(addr->personal);
  } else
     if(addr->mailbox)
	retc = g_strdup(addr->mailbox);
  
  return retc;
}

gchar *
ADDRESS_to_gchar (const ADDRESS * addr)
{
  gchar buf[1024]; /* assume no single address is longer than this */

  buf[0] = '\0';
  rfc822_write_address(buf, sizeof(buf), (ADDRESS*)addr);
  if(strlen(buf)>=sizeof(buf)-1)
    fprintf(stderr,
	    "ADDRESS_to_gchar: the max allowed address length exceeded.\n");
  return g_strdup(buf);
}

gchar *
make_string_from_list (GList * the_list)
{
  gchar *retc, *str;
  GList *list;
  GString *gs = g_string_new (NULL);
  Address *addy;

  list = g_list_first (the_list);

  while (list)
    {
      addy = list->data;
      str = address_to_gchar (addy);
      if(str) gs = g_string_append (gs, str);
      g_free (str);

      if (list->next)
	gs = g_string_append (gs, ", ");

      list = list->next;
    }
  retc = g_strdup (gs->str);
  g_string_free (gs, 1);
  return retc;
}

/* readfile allocates enough space for the ending '\0' characeter as well.
   returns the number of read characters.
*/
size_t
readfile (FILE * fp, char **buf)
{
  size_t size;
  off_t offset;
  int r;
  int fd = fileno (fp);
  struct stat statbuf;

  if (fstat (fd, &statbuf) == -1)
    return -1;

  size = statbuf.st_size;

  if (!size)
    {
      *buf = NULL;
      return size;
    }

  lseek (fd, 0, SEEK_SET);

  *buf = (char *) g_malloc (size+1);
  if (*buf == NULL)
    {
      return -1;
    }

  offset = 0;
  while (offset < size)
    {
      r = read (fd, *buf + offset, size - offset);
      if (r == 0)
	return offset;

      if (r > 0)
	{
	  offset += r;
	}
      else if ((errno != EAGAIN) && (errno != EINTR))
	{
	  perror ("Error reading file:");
	  return -1;
	}
    }
  (*buf)[size] = '\0';
  return size;
}

/* find_word:
   searches given word delimited by blanks or string boundaries in given
   string. IS NOT case-sensitive.
   Returns TRUE if the word is found.
*/
gboolean
find_word(const gchar * word, const gchar* str) {
    const gchar *ptr = str;
    int  len = strlen(word);
    
    while(*ptr) {
	if(g_strncasecmp(word, ptr, len) == 0)
	    return TRUE;
	/* skip one word */
	while(*ptr && !isspace( (unsigned char)*ptr) )
	    ptr++;
	while(*ptr && isspace( (unsigned char)*ptr) )
	    ptr++;
    }
    return FALSE;
}

/* wrap_string
   wraps given string replacing spaces with '\n'.  do changes in place.
   lnbeg - line beginning position, sppos - space position, 
   te - tab's extra space.
*/
void
wrap_string(gchar* str, int width)
{
   const int minl = width/2;
   gchar *lnbeg, *sppos, *ptr;
   gint te = 0;

   g_return_if_fail(str != NULL);
   lnbeg= sppos = ptr = str;

   while(*ptr) {
      if(*ptr=='\t') te += 7;
      if(*ptr==' ') sppos = ptr;
      if(ptr-lnbeg+1>width-te && sppos>=lnbeg+minl) {
	 *sppos = '\n';
	 lnbeg = ptr; te = 0;
      }
      if(*ptr=='\n') {
	 lnbeg = ptr; te = 0;
      }
      ptr++;
   }
}
