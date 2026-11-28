<div align="center" markdown="1">

# end4pC++

**A light, native desktop shell for Wayland, written in C++.**
Bar · launcher · notifications · control center · wallpaper · lock screen · widgets · its own Settings app.
No Qt, no QML, no GTK widgets: just `cairo` + `pango` in one small process.

![Hyprland](https://img.shields.io/badge/Hyprland-0.55%2B-58E1FF?style=flat-square)
![niri](https://img.shields.io/badge/niri-supported-9ece6a?style=flat-square)
![sway](https://img.shields.io/badge/SwayFX-supported-7aa2f7?style=flat-square)
![C++](https://img.shields.io/badge/C%2B%2B-meson%20%2B%20ninja-f7768e?style=flat-square)
![License](https://img.shields.io/badge/license-GPL--3.0-bb9af7?style=flat-square)
![Status](https://img.shields.io/badge/status-pre--alpha1-e0af68?style=flat-square)

[Install](docs/install.md) · [Features](docs/features.md) · [CLI reference](docs/commands.md) · [Keybinds](docs/keybinds.md) · [Configuration](docs/configuration.md) · [Languages](docs/languages.md) · [Troubleshooting](docs/troubleshooting.md)

</div>

<div align="center" markdown="1">

<img src="screenshots/1.png" alt="end4pC on Hyprland: bar, media player, weather, calendar and resources" width="100%">

</div>

---

It started as a C++ port of my Quickshell config and keeps its Material 3 look.

> [!WARNING]
> **Pre-alpha.** The project changes quickly and things may break. Tested on Arch Linux with Hyprland 0.57.1 (Lua config); niri and sway (SwayFX) are supported too.

---

## What is in it

- **Bar, panels and launcher:** the bar in six styles, launcher with clipboard history and emoji, control center, notifications, media card with lyrics, Alt+Tab switcher with live previews, power menu.
- **Wallpaper and themes:** local, Wallhaven, Unsplash and Pexels wallpapers, palettes generated from the wallpaper (matugen) or ready-made themes.
- **Capture and tools:** screenshots and recording with annotations, OCR, Google Lens and QR; Tools with a translator, notes, converters, timers, a key bind editor and more.
- **Desktop and system:** widgets, lock screen, game overlay, hot corners, Task Manager, accessibility, and a Settings app that configures almost everything.
- **Five small programs of their own**, in the same Material look: [**Play3r**](docs/play3r.md), a music player with synced lyrics; [**Fil3s**](docs/fil3s.md), a file manager with one-click conversions and archives; [**T3xt**](docs/t3xt.md), a text editor with syntax highlighting in the colours of the shell; [**Imag3s**](docs/imag3s.md), an image viewer where you can also draw on the picture; [**Vid3os**](docs/vid3os.md), a video player.

The full list is in [Features](docs/features.md).

## Install

On Arch Linux the installer does everything (it asks before it changes anything, and moves your old configs to a backup, never deletes them):

```bash
git clone https://github.com/pctrade/end4pC.git ~/.config/end4pC
~/.config/end4pC/installer/install.sh --dry-run    # shows what it would do, changes nothing
~/.config/end4pC/installer/install.sh              # asks before it does anything
```

Other distributions, the packages it needs, updating (`end4pC update`) and launching it: [Requirements and install](docs/install.md). The shell has to live in `~/.config/end4pC`.

## Documentation

| | |
|---|---|
| [Requirements and install](docs/install.md) | Packages, installer, building by hand, updating, launching |
| [Features](docs/features.md) | Everything the shell does |
| [Play3r](docs/play3r.md) · [Fil3s](docs/fil3s.md) · [T3xt](docs/t3xt.md) · [Imag3s](docs/imag3s.md) · [Vid3os](docs/vid3os.md) | The music player, the file manager, the text editor, the image viewer and the video player |
| [Command line reference](docs/commands.md) | Every `end4pC` command |
| [Keybinds](docs/keybinds.md) | The default binds and how to edit them |
| [Configuration](docs/configuration.md) | Where the settings live |
| [Languages](docs/languages.md) | Choosing and translating |
| [Troubleshooting](docs/troubleshooting.md) | When something does not work |

> [!NOTE]
> The shell is also its own command line: every panel, widget and setting can be driven from a terminal or a key bind. See the [command line reference](docs/commands.md).

---

## Credits

- Inspired by [end-4's dots-hyprland](https://github.com/end-4/dots-hyprland) (illogical-impulse) and its Material look.
- Palettes from the wallpaper: [matugen](https://github.com/InioX/matugen).
- Icons: [Solar Icon Set](https://github.com/480-Design/Solar-Icon-Set) (CC BY 4.0). Glyphs: Material Symbols. Shapes: Material 3 expressive shapes.
- Dotfiles of the installer: based on end-4's dots-hyprland (GPL-3.0). More in [THIRD_PARTY.md](THIRD_PARTY.md).

## License

end4pC is free software under the [GNU General Public License v3.0](LICENSE). Copyright (C) 2026 pctrade.

The icons, fonts and configs it includes keep their own licenses: see [THIRD_PARTY.md](THIRD_PARTY.md).
