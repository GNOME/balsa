#define STOCK_TOOLBAR_COUNT 3

int create_stock_toolbar(int id);
int get_toolbar_index(int id);
void set_toolbar_button_callback(int toolbar, char *id, void (*callback)(GtkWidget *, gpointer), gpointer);
void set_toolbar_button_sensitive(GtkWidget *window, int toolbar, char *id, int sensitive);
GtkToolbar *get_toolbar(GtkWidget *window, int toolbar);
void release_toolbars(GtkWidget *window);
GtkWidget *get_tool_widget(GtkWidget *window, int toolbar, char *id);
void update_all_toolbars(void);
char **get_legal_toolbar_buttons(int toolbar);
