[← end4pC docs](README.md)

# Vid3os: the video player

A light video player, a program of its own (`end4pC-vid3os`, or `end4pC vid3os [video]`). It only plays: no editing. [libmpv](https://mpv.io) decodes and draws the video, so it plays what mpv plays (MP4, MKV, WebM, AVI, MOV, subtitles, several audio tracks...); the controls are drawn by the shell in its own look and **go away by themselves while the video plays** (and the pointer with them): move the mouse, or pause, and they come back.

## Launching it

- **Application menu / launcher:** look for **Vid3os**; `end4pC update` and the installer add its entry (it needs `libmpv`, which the `mpv` package brings). From a terminal or a key bind: `end4pC vid3os`, `end4pC vid3os ~/Videos/film.mkv`; in Tools > Keybinds it is a Shell action. It is offered in Settings > System > Default Apps as the **Video player**, and in Tools > Autostart.
- It opens floating and centred (there is a Hyprland rule for it, like the one of mpv).
- It plays files that are on disk (not web addresses), without mpv's scripts, config file or youtube-dl.

## Keys and mouse

- **Play / pause:** Space or K, or a click on the video. **Full screen:** F / F11 or a double click; Esc leaves it.
- **Seek:** Left / Right 5 s (Shift: 30 s), J / L 10 s, Home to the start, or click or drag the bar of time.
- **Volume:** Up / Down or the wheel, M mutes (the volume is remembered). **Speed:** the chip at the right goes through 0.5× to 2×, or `[` / `]`.
- **Subtitles / audio track:** S and A (or the buttons) go to the next track and say which one it is; the headphones are crossed out when the audio is off. Subtitles in the same folder with the same name are loaded.
- **Folder:** N / P (Page Down / Page Up, or the buttons) go to the next and previous video of the folder. **Continuous playback:** when a video ends the next one of the folder starts by itself; the playlist button (or C) at the right of the controls turns that on or off (it is on by default, lit while on, and remembered). Ctrl+O opens a file, Q or Ctrl+W closes.
