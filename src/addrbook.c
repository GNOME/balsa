#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "balsa-app.h"
#include "addrbook.h"

AddressBook *
addressbook_read_pine (gchar * file)
{
  int fd, ret, i, j = 0;

  int ine = 0, tabnum = 0;

  char buff[4096];
  char tmp[4096];
  fd = open (file, O_RDONLY);
  if (fd == -1)
    {
      perror ("error opening address book file");
      return FALSE;
    }
  ret = read (fd, buff, 4096);
  if (ret > 0)
    buff[ret - 1] = '\0';
  g_print ("%s\n", buff);

  for (i = 0; i < ret; i++)
    {
      if (buff[i] == '\r' && buff[i + 1] == '\n')
	{
	  tmp[j] = '\0';
	  g_print ("%i: %s\n", tabnum, tmp);
	  *tmp = '\0';
	  j = 0;
	  i++;
	  if (buff[i + 1] == ' ' && buff[i + 2] == ' ' && buff[i + 3] == ' ')
	    i += 3;
	  else
	    tabnum = 0;
	}
      else if (buff[i] == '\n' && buff[i + 1] == '\r')
	{
	  tmp[j] = '\0';
	  g_print ("%i: \"%s\"\n", tabnum, tmp);
	  *tmp = '\0';
	  j = 0;
	  i++;
	  if (buff[i + 1] == ' ' && buff[i + 2] == ' ' && buff[i + 3] == ' ')
	    i += 3;
	  else
	    tabnum = 0;
	}
      else if (buff[i] == '\n')
	{
	  tmp[j] = '\0';
	  if (j < 1)
	    {
	      g_print ("%i: \"%s\"\n", tabnum, tmp);
	      *tmp = '\0';
	      j = 0;
	      if (buff[i + 1] == ' ' && buff[i + 2] == ' ' && buff[i + 3] == ' ')
		i += 3;
	      else
		tabnum = 0;
	    }
	}
      else if (buff[i] == '\r')
	{
	  tmp[j] = '\0';
	  g_print ("%i: %s\n", tabnum, tmp);
	  *tmp = '\0';
	  j = 0;
	  if (buff[i + 1] == ' ' && buff[i + 2] == ' ' && buff[i + 3] == ' ')
	    i += 3;
	  else
	    tabnum = 0;
	}
      else
	switch (buff[i])
	  {
	  case '\t':
	    if (strlen (tmp) < 1)
	      break;
	    tmp[j] = '\0';
	    g_print ("%i: \"%s\"\n", tabnum, tmp);
	    *tmp = '\0';
	    j = 0;
	    tabnum++;
	    break;
	  case '(':
	    ine = 1;
	    break;
	  case ')':
	    ine = 0;
	    break;
	  case '\0':
	    tmp[j] = '\0';
	    g_print ("%i: \"%s\"\n", tabnum, tmp);
	    break;
	  default:
	    tmp[j] = buff[i];
	    j++;
	    break;
	  }
    }

  close (fd);
  return NULL;
}
