#ifndef __IMAP_SERVER_H__
#define __IMAP_SERVER_H__

#define LIBBALSA_TYPE_IMAP_SERVER \
    (libbalsa_imap_server_get_type())
#define LIBBALSA_IMAP_SERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST(obj, LIBBALSA_TYPE_IMAP_SERVER, \
				LibBalsaImapServer))
#define LIBBALSA_IMAP_SERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST(klass, LIBBALSA_TYPE_IMAP_SERVER, \
                             LibBalsaImapServerClass))
#define LIBBALSA_IS_IMAP_SERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE(obj, LIBBALSA_TYPE_IMAP_SERVER))
#define LIBBALSA_IS_IMAP_SERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE(klass, LIBBALSA_TYPE_IMAP_SERVER))

GType libbalsa_imap_server_get_type(void);
typedef struct LibBalsaImapServer_ LibBalsaImapServer;

LibBalsaImapServer* libbalsa_imap_server_new(const gchar *username,
					     const gchar *host);
LibBalsaImapServer* libbalsa_imap_server_new_from_config(void);
ImapMboxHandle* libbalsa_imap_server_get_handle(LibBalsaImapServer *server);
ImapMboxHandle* libbalsa_imap_server_get_handle_with_user(
				LibBalsaImapServer *imap_server,
                                gpointer user);
void libbalsa_imap_server_release_handle(LibBalsaImapServer *server,
					 ImapMboxHandle* handle);
void libbalsa_imap_server_set_max_connections(LibBalsaImapServer *server,
					      int max);
void libbalsa_imap_server_force_disconnect(LibBalsaImapServer *server);
void libbalsa_imap_server_close_all_connections(void);
gboolean libbalsa_imap_server_has_free_handles(LibBalsaImapServer *server);
gboolean libbalsa_imap_server_is_offline(LibBalsaImapServer *server);
void libbalsa_imap_server_set_offline_mode(LibBalsaImapServer *server,
					   gboolean offline);

extern gint ImapDebug;
#endif /* __IMAP_SERVER_H__ */
