#ifndef __IMAP_PRIVATE_H__
#define __IMAP_PRIVATE_H__

#include <glib-object.h>

struct _ImapMboxHandle {
  GObject object;

  int sd; /* socket descriptor */
  struct siobuf * sio;
  char *host;
  int  port;
  char *user;
  char *passwd;
  char* mbox;

  ImapMboxHandleState state;
  gboolean has_capabilities;
  ImapCapability capabilities[IMCAP_MAX];
  unsigned exists;
  unsigned recent;
  unsigned unseen;
  ImapUID  uidnext;
  ImapUID  uidval;
  gboolean readonly_mbox;

  ImapMessage **msg_cache;
  /* BYE handling depends on the state */
  gboolean doing_logout;

  ImapInfoCb info_cb;
  void *info_arg;

  ImapMonitorCb monitor_cb;
  void *monitor_arg;

  ImapFlagsCb flags_cb;
  void *flags_arg;

  ImapInfoCb alert_cb;
  void *alert_arg;
};

extern const char* msg_flags[];
#endif /* __IMAP_PRIVATE_H__ */
