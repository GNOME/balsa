balsa_SOUNDS = startup.wav newmail.wav

balsasoundsdir = $(datadir)/sounds/balsa
balsasounds_DATA = $(balsa_SOUNDS)

soundlistdir = $(sysconfdir)/sound/events
soundlist_in_files = balsa.soundlist.in
soundlist_DATA = $(soundlist_in_files:.soundlist.in=.soundlist)
@INTLTOOL_SOUNDLIST_RULE@

EXTRA_DIST = $(balsa_SOUNDS) $(soundlist_in_files)

DISTCLEANFILES = $(soundlist_DATA)
