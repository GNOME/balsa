#include "config.h"
#include <gnome.h>
#include "balsa-app.h"
#include "balsa-init.h"
#include "pref-manager.h"

void 
initialize_balsa (int argc, char *argv[])
{
      fprintf (stderr,"New install of Balsa\n");

      open_preferences_manager();
}
