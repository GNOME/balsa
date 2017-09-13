# sounds/meson.build

balsa_sounds = ['startup.wav', 'newmail.wav']

balsasoundsdir = join_paths(get_option('datadir'), 'sounds', 'balsa')
install_data(balsa_sounds, install_dir : balsasoundsdir)

soundlistdir = join_paths(get_option('sysconfdir'), 'sound', 'events')
i18n.merge_file(input       : 'balsa.soundlist.in',
                output      : 'balsa.soundlist',
                type        : 'desktop',
                po_dir      : '../po',
                install     : true,
                install_dir : soundlistdir)
