
#ifndef __libimap_MARSHAL_H__
#define __libimap_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:INT,CHAR,POINTER (libimap-marshal.list:1) */
extern void libimap_VOID__INT_CHAR_POINTER (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

G_END_DECLS

#endif /* __libimap_MARSHAL_H__ */

