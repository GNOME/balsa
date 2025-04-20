# sounds/meson.build

balsa_sounds = ['startup.wav', 'newmail.wav']

balsa_sounds_dir = join_paths(get_option('datadir'), 'sounds', 'balsa')
install_data(balsa_sounds, install_dir : balsa_sounds_dir)

sound_list_dir = join_paths(get_option('sysconfdir'), 'sound', 'events')

# if i18n_merge knew about the 'description' keyword, we would use:
# i18n.merge_file(input       : 'balsa.soundlist.meson',
#                 output      : 'balsa.soundlist',
#                 type        : 'desktop',
#                 po_dir      : '../po',
#                 install     :  true,
#                 install_dir :  sound_list_dir)
# instead:
custom_target('soundlist-file',
              input       : 'balsa.soundlist.in',
              output      : 'balsa.soundlist',
              install     : true,
              install_dir : sound_list_dir,
              command     : ['msgfmt', '--desktop',
                             '--keyword=description',
                             '--template=@INPUT@',
                             '-d', join_paths(meson.current_source_dir(), '../po'),
                             '--output-file=@OUTPUT@'])
