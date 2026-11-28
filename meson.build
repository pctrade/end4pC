# buildtype debugoptimized (-O2 -g): the plain debug build (-O0) made every redraw several times slower
project('end4pC', ['c', 'cpp'], default_options: ['cpp_std=c++20', 'warning_level=2', 'buildtype=debugoptimized'])

fs = import('fs')
add_project_arguments('-DEND4PC_VERSION="' + fs.read('VERSION').strip() + '"', language: ['c', 'cpp'])  # shown by `end4pC --version`; the label of the installed version
cc = meson.get_compiler('c')
wayland_client = dependency('wayland-client')
pangocairo = dependency('pangocairo')
pangoft2 = dependency('pangoft2')
fontconfig = dependency('fontconfig')  # the shell font is chosen by an alias for "Sans" (src/shellfont.h)
gdkpixbuf = dependency('gdk-pixbuf-2.0')
rsvg = dependency('librsvg-2.0')
threads = dependency('threads')  # wallpaper decoding for transitions runs in worker threads
sdbus = dependency('libsystemd')  # sd-bus: the notification service talks D-Bus
pam = cc.find_library('pam')
xkb = dependency('xkbcommon')  # turns key codes into text for the launcher's search box

scanner = find_program('wayland-scanner')
xdg_xml = '/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml'
layer_xml = '/usr/share/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml'
cursor_xml = '/usr/share/wayland-protocols/staging/cursor-shape/cursor-shape-v1.xml'
tablet_xml = '/usr/share/wayland-protocols/stable/tablet/tablet-v2.xml'
lock_xml = '/usr/share/wayland-protocols/staging/ext-session-lock/ext-session-lock-v1.xml'  # cursor-shape refers to its types
idle_xml = '/usr/share/wayland-protocols/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'
ftl_xml = '/usr/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml'  # the overview: live windows
capsrc_xml = '/usr/share/wayland-protocols/staging/ext-image-capture-source/ext-image-capture-source-v1.xml'
copycap_xml = '/usr/share/wayland-protocols/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml'
blur_xml = '/usr/share/wayland-protocols/staging/ext-background-effect/ext-background-effect-v1.xml'  # blur behind translucent shell surfaces (niri)
fscale_xml = '/usr/share/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml'  # sharp drawing at fractional scales (src/hidpi.h)
viewporter_xml = '/usr/share/wayland-protocols/stable/viewporter/viewporter.xml'

generated = []
foreach xml : [xdg_xml, layer_xml, cursor_xml, tablet_xml, lock_xml, idle_xml, blur_xml, ftl_xml, capsrc_xml, copycap_xml, fscale_xml, viewporter_xml]
  name = fs.stem(xml)
  generated += custom_target(name + '-header',
    input: xml,
    output: name + '-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  generated += custom_target(name + '-code',
    input: xml,
    output: name + '-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

executable('end4pC', 'src/main.cpp', 'src/notify_dbus.c', 'src/tray_dbus.c', 'src/polkit_dbus.c', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, sdbus, threads],
  install: false)

executable('end4pC-settings', 'src/settings.cpp', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb],
  install: false)

text_input_xml = '/usr/share/wayland-protocols/unstable/text-input/text-input-unstable-v3.xml'
text_input = [
  custom_target('text-input-v3-header', input: text_input_xml, output: 'text-input-unstable-v3-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target('text-input-v3-code', input: text_input_xml, output: 'text-input-unstable-v3-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]
executable('end4pC-tools', 'src/tools.cpp', generated, text_input,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, threads],
  install: false)

executable('end4pC-lock', 'src/lock.cpp', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, pam, threads],
  install: false)

# Test build: unlocks itself after 16 seconds. Never installed or bound to a key.
executable('end4pC-lock-debug', 'src/lock.cpp', generated,
  cpp_args: ['-DLOCK_DEBUG=16'],
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, pam, threads],
  install: false)

# Where the desktop widgets are placed on screens of many sizes (src/deskplace.h): `meson test -C build`, or tests/desk/run.sh
test('deskplace', executable('deskplace-test', 'tests/desk/test_deskplace.cpp', install: false))

# The Markdown formatting of the Notes editor (src/mdedit.h): `meson test -C build`, or tests/md/run.sh
test('mdedit', executable('mdedit-test', 'tests/md/test_mdedit.cpp', install: false))

# The single-line text selection of the text fields (src/textsel.h): `meson test -C build`, or tests/text/run.sh
test('textsel', executable('textsel-test', 'tests/text/test_textsel.cpp', install: false))

# The geometry of the display arrangement editor (src/displaymap.h): `meson test -C build`, or tests/display/run.sh
test('displaymap', executable('displaymap-test', 'tests/display/test_displaymap.cpp', install: false))

# The Freeform board of the Tools window: shapes, concept links, undo and the file form (src/freeform.h): `meson test -C build`, or tests/freeform/run.sh
test('freeform', executable('freeform-test', 'tests/freeform/test_freeform.cpp', install: false))

# The per-monitor hot corner rules (src/hotcorners.h): `meson test -C build`, or tests/hotcorners/run.sh
test('hotcorners', executable('hotcorners-test', 'tests/hotcorners/test_hotcorners.cpp', install: false))

# What the graphics card is doing, from sysfs and nvidia-smi (src/gpuinfo.h): `meson test -C build`, or tests/gpu/run.sh
test('gpuinfo', executable('gpuinfo-test', 'tests/gpu/test_gpuinfo.cpp', install: false))

# The order and visibility of the Tools tabs (src/tools_tabs.h): `meson test -C build`, or tests/tools_tabs/run.sh
test('tools_tabs', executable('tools_tabs-test', 'tests/tools_tabs/test_tools_tabs.cpp', install: false))

# The workspaces a bar shows with several screens (src/wsmonitors.h): `meson test -C build`, or tests/wsmonitors/run.sh
test('wsmonitors', executable('wsmonitors-test', 'tests/wsmonitors/test_wsmonitors.cpp', install: false))

# The display name and the host name (src/identity.h): `meson test -C build`, or tests/identity/run.sh
test('identity', executable('identity-test', 'tests/identity/test_identity.cpp', install: false))

# Headless media integration: real shell code with a temporary fake playerctl.
test('media', executable('media-test', 'tests/media/test_media.cpp',
  'src/notify_dbus.c', 'src/tray_dbus.c', 'src/polkit_dbus.c', generated,
  cpp_args: ['-O0', '-g0'],
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, sdbus, threads],
  install: false))

# Play3r: the music player. ffmpeg decodes, PipeWire plays, TagLib reads the tags (src/player/). It is built only when the three libraries are there:
# a machine without them gets the rest of the shell (and no Play3r) instead of a failed build.
pipewire = dependency('libpipewire-0.3', required: false)
avformat = dependency('libavformat', required: false)
avcodec = dependency('libavcodec', required: false)
avutil = dependency('libavutil', required: false)
swresample = dependency('libswresample', required: false)
taglib = dependency('taglib', required: false)
if pipewire.found() and avformat.found() and avcodec.found() and avutil.found() and swresample.found() and taglib.found()
  player_audio = [pipewire, avformat, avcodec, avutil, swresample, taglib, threads]

  executable('end4pC-play3r', 'src/player/player.cpp', 'src/player/engine.cpp', 'src/player/mpris.cpp', generated,
    dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, sdbus, player_audio],
    install: false)

  # The audio engine of Play3r without a sound card: `meson test -C build` (it plays a file of ~/Music through a fake output and skips without one)
  test('player_engine', executable('player-engine-test', 'tests/player/test_engine.cpp', 'src/player/engine.cpp', dependencies: player_audio, install: false), timeout: 60)
else
  message('Play3r (the music player) is not built: it needs libpipewire, ffmpeg (libavformat, libavcodec, libswresample) and taglib')
endif

# Fil3s: the file manager (src/files/). Light on purpose: no libraries beyond the ones every window of the shell already uses.
gio = dependency('gio-2.0')
executable('end4pC-fil3s', 'src/files/fil3s.cpp', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, threads, gio],
  install: false)

# T3xt: the text editor (src/editor/). Its own libraries are only the ones every window of the shell uses; the highlighter and the document are tested alone.
executable('end4pC-t3xt', 'src/editor/editor.cpp', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, threads],
  install: false)
test('editor', executable('editor-test', 'tests/editor/test_editor.cpp', cpp_args: ['-O1'], install: false), timeout: 30)

# Imag3s: the image viewer with annotations (src/images/). The model of the edits (marks, undo, turn and mirror) is tested alone.
executable('end4pC-imag3s', 'src/images/images.cpp', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, threads, gio],
  install: false)
test('images', executable('images-test', 'tests/images/test_images.cpp', cpp_args: ['-O1'], install: false), timeout: 30)

# Vid3os: the video player (src/video/). libmpv decodes and renders in software into the shared-memory buffers; the controls are drawn over it.
mpv_lib = dependency('mpv', required: false)
if mpv_lib.found()
  executable('end4pC-vid3os', 'src/video/vid3os.cpp', generated,
    dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, threads, mpv_lib],
    install: false)
  test('video', executable('video-test', 'tests/video/test_video.cpp', cpp_args: ['-O1'], install: false), timeout: 30)
endif
