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
  int fd, ret;
  char buff[4096];
  fd = open (file, O_RDONLY);
  if (fd == -1)
    {
      perror ("error opening signature file");
      return FALSE;
    }
  ret = read (fd, buff, 4096);
/*if (ret > 0) */
  printf ("%s\n", buff);
  close (fd);
}
