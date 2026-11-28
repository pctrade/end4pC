[← end4pC docs](README.md)

# Requirements and install

### To build

On Arch (package names):

```text
meson ninja gcc pkgconf
wayland wayland-protocols wlr-protocols
cairo pango gdk-pixbuf2 librsvg
libxkbcommon pam systemd-libs
ffmpeg taglib pipewire        # Play3r only
```

`ffmpeg`, `taglib` and `pipewire` are only for Play3r: without them everything else builds and Play3r is skipped (the build says so).

The build looks for the protocol XML files in `/usr/share/wayland-protocols` and `/usr/share/wlr-protocols`. It needs `wayland-protocols` 1.37 or newer: the overview and the window switcher use `ext-image-copy-capture`.

### To run

- `hyprland` **0.55 or newer** (the rules and binds in this repo are Lua), or niri, or sway/SwayFX.
- Helper tools. Most are optional: a missing one only disables the feature that uses it.

| Feature | Packages |
|---|---|
| Palette from the wallpaper | `matugen`, `python` |
| AI usage in the bar | `python` (reads `~/.claude` and `~/.codex`; nothing is sent anywhere) |
| Screenshots, recording | `grim`, `slurp`, `wf-recorder`, `wl-clipboard`, `swappy` (optional editor), `jq` |
| OCR, QR | `tesseract`, `zxing-cpp` or `zbar` (reading QR codes) |
| Audio, microphone | `wireplumber` (`wpctl`), `pipewire-pulse` (`pactl`) |
| Fil3s conversions and archives | `imagemagick`, `ffmpeg`, `zip`, `7zip`, `libarchive` (bsdtar), optionally `rar` |
| Media player, Play3r | `playerctl`, `cava` (optional visualizer); Play3r also uses `zenity` (folder chooser) and `python` (lyrics) |
| Wi-Fi, Bluetooth, battery | `networkmanager`, `bluez-utils`, `upower` |
| Power profile | `power-profiles-daemon` |
| Brightness | `ddcutil` (external monitors) or `brightnessctl` |
| Online wallpaper keys | `libsecret` (`secret-tool`) and a keyring such as `gnome-keyring` |
| Clipboard history | `cliphist` |
| File choosers, thumbnails | `zenity`, `imagemagick` |
| File search | `fd` |
| Removable drives | `udisks2` |
| Weather | `curl` (Open-Meteo, no key; the city is found by your IP unless you set one) |
| Night light | `hyprsunset` or `wlsunset` |
| Updates counter | `pacman-contrib` (`checkupdates`), `paru` or `yay`, `flatpak` |
| Score (Tools) | `python` (standard library only), `wl-clipboard` (to copy a booking code) |

> [!IMPORTANT]
> Stop any other bar, notification daemon or wallpaper tool first (waybar, mako, swaync, swww, Quickshell...). end4pC owns the notification service, the tray and the wallpaper. Do not run another polkit agent (polkit-kde, polkit-gnome, hyprpolkitagent...) at the same time: only one can be registered per session.

## Install

The shell has to live in **`~/.config/end4pC`**: the Hyprland, niri and sway files of this repository point there.

### Arch Linux: the installer

```bash
git clone https://github.com/pctrade/end4pC.git ~/.config/end4pC
~/.config/end4pC/installer/install.sh --dry-run    # shows what it would do, changes nothing
~/.config/end4pC/installer/install.sh              # asks before it does anything
```

It installs the packages, sets up the compositors you pick (Hyprland, niri, sway) with their configs and dotfiles, and builds the shell. A config folder you already have is moved to `~/.local/share/end4pC-installer/backup-<date>/`, never deleted. Details and options: [installer/README.md](../installer/README.md).

### Any other distribution: build it yourself

```bash
git clone https://github.com/pctrade/end4pC.git ~/.config/end4pC
cd ~/.config/end4pC
meson setup build
ninja -C build
```

Binaries produced in `build/`:

| Binary | What it is |
|---|---|
| `end4pC` | The shell (bar, popups, wallpaper, widgets) **and** its own command line |
| `end4pC-settings` | The Settings window |
| `end4pC-tools` | The Tools window |
| `end4pC-play3r` | The music player |
| `end4pC-fil3s` | The file manager |
| `end4pC-t3xt` | The text editor |
| `end4pC-imag3s` | The image viewer |
| `end4pC-vid3os` | The video player (needs `libmpv`, in the `mpv` package) |
| `end4pC-lock` | The lock screen |

<details>
<summary><b>Connect Hyprland</b></summary>

Add this block to your `~/.config/hypr/hyprland.lua`:

```lua
-- end4pC shell: layer rules, keybinds and autostart
do
    local ok, err = pcall(dofile, os.getenv("HOME") .. "/.config/end4pC/hyprland/end4pC.lua")
    if not ok then print("end4pC: " .. tostring(err)) end
end
```

[`hyprland/end4pC.lua`](hyprland/end4pC.lua) holds the layer rules (blur, animations), the window rule of the Settings app, loads the key binds ([`hyprland/binds.lua`](hyprland/binds.lua)) and **starts the shell with the session**. Read it before using it: some binds replace ones you may already have (`SUPER+L`, `SUPER+P`).

Machine-specific rules go in `hyprland/local.lua`: it is loaded when it exists and is not in git. Then `hyprctl reload`, or log out and in.

</details>

<details>
<summary><b>Connect niri or sway</b></summary>

See [niri/README.md](../niri/README.md) and [sway/README.md](../sway/README.md).

</details>

### Update

```bash
end4pC update              # shows what is new and asks before applying it
end4pC update --check      # only says whether there is something new
end4pC update -y           # applies it without asking
end4pC update --rollback   # goes back to the version that ran before the last update
end4pC --version
```

It follows the `stable` branch (`git fetch` + fast-forward, then `ninja`) and only touches the shell's own code and assets. Your settings, dotfiles and compositor configs are never changed. Shell files you edited yourself are set aside instead of overwritten. Restart the shell afterwards (`SUPER + R`).

### Lock screen

The lock screen checks your password through PAM. It uses `/etc/pam.d/end4pC-lock` if it exists, otherwise `hyprlock`, otherwise `login`, so on most systems it works without setup.

## Launch

Once the block above is in your Hyprland config, **the shell starts with the session**. By hand:

```bash
end4pC run                          # detached; does nothing if it is already running
~/.config/end4pC/build/end4pC       # foreground, prints its log (useful when debugging)
end4pC restart                      # or SUPER + R, works even if the shell is stuck
```

Open Settings from the bar's gear, with `SUPER + Escape`, or `end4pC settings`.
