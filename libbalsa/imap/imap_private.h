#ifndef __IMAP_PRIVATE_H__
#define __IMAP_PRIVATE_H__

#include <glib-object.h>

struct _MboxView {
  unsigned *arr;
  unsigned allocated, entries;
  char * filter_str;
};

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

  ImapMessage **msg_cache;
  MboxView mbox_view;
  GNode *thread_root; /* deprecated! */

  /* BYE handling depends on the state */
  gboolean doing_logout;
  ImapInfoCb info_cb;
  void *info_arg;
  ImapMonitorCb monitor_cb;
  void *monitor_arg;
  ImapFlagsCb flags_cb;
  void *flags_arg;
  ImapFetchBodyCb body_cb;
  void *body_arg;

  ImapInfoCb alert_cb;
  void *alert_arg;

  ImapSearchCb search_cb;
  void *search_arg;
  unsigned readonly_mbox:1;
};

extern const char* msg_flags[];

void imap_mbox_resize_cache(ImapMboxHandle *h, unsigned new_size);

#endif /* __IMAP_PRIVATE_H__ */
