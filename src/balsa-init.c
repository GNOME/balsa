#include "balsa-init.h"

void 
initialize_balsa ()
{
  if (!gnome_config_has_section ("/balsa/Global"))
    {
      printf ("New install of Balsa\n");
      open_preferences_manager();
    }
}
