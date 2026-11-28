[← end4pC docs](README.md)

# Command line reference

`end4pC` with no arguments starts the shell. With arguments it **talks to the running shell** over a socket and prints the reply, so everything here works from a terminal, a key bind or a script.

> [!IMPORTANT]
> **Put the whole command in quotes** when it has more than one word: `end4pC "volume up"`, `end4pC "brightness set 70"`, `end4pC "set dock 1"`. Without the quotes only the first word reaches the shell (`end4pC volume up` is read as `end4pC volume`). Commands of one word, like `end4pC settings` or `end4pC ping`, need no quotes. Every example below with arguments is meant to be written this way.

```text
end4pC                          start the shell (foreground)
end4pC run                      start it detached (no-op if already running)
end4pC <command> [args]         send a command to the running shell
end4pC --version                installed version
end4pC help                     the built-in summary
```

### Shell and settings

| Command | Does |
|---|---|
| `ping` | Prints `pong` when the shell runs |
| `restart` / `quit` | Restart or stop the shell |
| `config` | Print the settings in use |
| `set <key> <value>` | Change any key of `config.json` and apply it live |
| `applytheme [image]` | Regenerate the palette from the wallpaper (no path: the one in use) |
| `wallpaper <image>` | Change the wallpaper |
| `collage [args]` | The wallpaper collage |
| `shapes` | List the Material 3 shapes accepted by the `*Shape` settings |
| `accentpick` | Pick an accent color from the screen |
| `update [--check\|-y\|--rollback]` | Update the shell (see [Update](install.md#update)) |

```bash
end4pC set barPosition bottom
end4pC set barStyle notch
end4pC set selectorShape cookie9
end4pC set dock 1
```

### Panels and popups

All take `toggle` (the default), `open` or `close` unless noted.

| Command | Panel |
|---|---|
| `launcher [toggle\|open\|close\|clipboard\|emoji\|query <text>]` | Launcher; `clipboard` and `emoji` open it in that mode |
| `selector` | Wallpaper selector |
| `control [toggle\|open\|close\|wifi\|bt]` | Control center (`wifi` / `bt` jump to that page) |
| `media` | Media player card |
| `panel` | Notification panel |
| `power` | Power menu |
| `switcher [open\|next\|prev\|commit\|close]` | Alt+Tab window switcher |
| `clockpanel`, `weatherpanel`, `aipanel`, `respanel`, `batterypanel` `[toggle\|open\|close\|tab N]` | Popups of the bar's widgets |
| `audiomenu [open\|close\|tab output\|input\|apps]` | Volume popup |
| `desktopmenu [open [x y]\|close]` | The desktop's right-click menu |
| `drives` | Removable drives popup (only while a drive is plugged in) |
| `game [open\|close]` | Game overlay |
| `settings` | Settings window |
| `tools` | Tools window |
| `play3r [song or folder]` | The music player: opens it (closes it when open); with a path it plays it |
| `fil3s [folder]` | The file manager: a new window, on that folder if given |
| `t3xt [file]` | The text editor: a new window, with that file open if given |
| `imag3s [picture]` | The image viewer: a new window, with that picture open if given |
| `vid3os [video]` | The video player: a new window, playing that video if given |
| `tasks` | Task Manager window |
| `osd volume \| osd brightness` | Show the on-screen display |

### Desktop widgets

| Command | Does |
|---|---|
| `weather\|player\|calendar\|resources\|image\|clock\|todo\|usercard\|text\|visualizer [toggle\|show\|hide]` | Show or hide one widget |
| `widget <name> on\|off [W H LEFT TOP]` | Show or hide it, with that size and position |
| `widgets lock\|unlock` | Lock or unlock the edit mode (drag and resize) |
| `usercard image [path]` | The user card's picture (no path: back to the wallpaper) |

### Bar, dock, tray and hardware

| Command | Does |
|---|---|
| `dock [on\|off\|toggle]` | The dock |
| `tray` | List the tray items |
| `tray match` | Which window each tray item maps to |
| `tray click <n>` | Click tray item `n` |
| `tray open` / `tray close` | Expand or collapse the tray |
| `brightness [get\|up\|down\|set N]` | Screen brightness |
| `volume [get\|up\|down\|set N\|mute\|unmute\|toggle]` | Volume of the default speakers (`up` and `down` use the step of the Settings; the answer is the volume it ends at) |
| `dnd [on\|off\|toggle]` | Do not disturb |
| `updates [check]` | Check pending updates now |
| `quick <n>` | Run quick action `n` of the bar |
| `identify [NAME=N ...]` | Flash a number on each screen (and assign names) |
| `lock` | Lock the screen |

### Capture, OCR and QR

| Command | Does |
|---|---|
| `screenshot [region\|screen\|window\|edit]` | Take a screenshot (default: `region`, with annotations) |
| `screenshot ocr` | Select an area and copy its text (needs `tesseract`) |
| `screenshot lens` | Select an area and search it with Google Lens |
| `screenshot qr` | Select an area and read the QR code in it |
| `qr <text>` | Show the QR code of a text or link |
| `recording` | Prints `recording` or `idle` |

### Accessibility

| Command | Does |
|---|---|
| `osk [toggle\|open\|close]` | On-screen keyboard; it opens on the focused monitor |
| `magnifier [get\|in\|out\|reset\|set N]` | Screen magnifier (Hyprland cursor zoom) |

### Timers, stopwatch, reminders

They keep running and ring with every window closed.

```bash
end4pC "timer add 25:00 pomodoro"     # 5m, 1h30m, 90 (seconds) or MM:SS
end4pC "timer pause|resume|reset|remove|clear|list"
end4pC "stopwatch start|pause|reset|lap|get"
end4pC "reminder add 08:30 weekdays stand up"   # once | daily | weekdays | YYYY-MM-DD
end4pC "reminder remove|toggle|list"
```

### Introspection (for scripts)

| Command | Prints |
|---|---|
| `workspace` | The current workspace state |
| `music` | The current media player state |
| `notifs` | The pending notifications |
| `recording` | Whether a recording is running |
| `config` | The main settings in use |

### Debug

Fake states for testing the bar; they last until the next real event.

```text
network <kind>                       force the network icon
notifications <n>                    force the notification count
periph <pct> <state> <kind> [name]   fake a peripheral battery; `periph off` clears it
```

> [!TIP]
> Quote the command when it has arguments so the shell hands it over as one string: `end4pC "launcher query firefox"`.
