#include "config.h"
#include <gnome.h>
#include "balsa-init.h"

void 
initialize_balsa (int argc, char *argv[])
{
      fprintf (stderr,"New install of Balsa\n");

      init_balsa_app (argc, argv);

      open_preferences_manager();
}
