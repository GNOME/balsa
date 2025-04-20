balsa_SOUNDS = startup.wav newmail.wav

balsasoundsdir = $(datadir)/sounds/balsa
balsasounds_DATA = $(balsa_SOUNDS)

soundlistdir = $(sysconfdir)/sound/events
soundlist_in_files = balsa.soundlist.in
soundlist_DATA = $(soundlist_in_files:.soundlist.in=.soundlist)

EXTRA_DIST = $(balsa_SOUNDS) $(soundlist_in_files) \
	meson.build

DISTCLEANFILES = $(soundlist_DATA)

%.soundlist:	%.soundlist.in
	$(AM_V_GEN)$(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@
