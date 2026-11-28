[← end4pC docs](README.md)

# Imag3s: the image viewer

A light image viewer, a program of its own (`end4pC-imag3s`, or `end4pC imag3s [picture]`), with a small editor to draw on the picture: pen, marker, arrows, rectangles, ellipses, text and a pixelate tool to hide things in a screenshot. It looks like the rest of the shell (the colours follow your wallpaper and theme). No toolkit, no plugins.

## Launching it

- **Application menu / launcher:** look for **Imag3s**; `end4pC update` and the installer add its entry (it needs no new libraries). From a terminal or a key bind: `end4pC imag3s`, `end4pC imag3s ~/Pictures/photo.jpg`; in Tools > Keybinds it is a Shell action. To open pictures with it by default: `xdg-mime default end4pC-imag3s.desktop image/png image/jpeg` (optional).
- Without a picture it asks for one. It can open whatever your `gdk-pixbuf` loaders read (PNG, JPEG, WebP, GIF, BMP, TIFF, SVG, AVIF, ICO... depending on what is installed). Photos taken sideways are turned the right way up; a picture over 100 megapixels is refused.

## Viewing

- **Moving pictures:** a GIF or an animated WebP plays at the pace its file says; the small button beside the zoom chip pauses and plays it. While you draw on it (or after it has edits) it stops on the frame that was showing, and the saved copy is that frame.

- **Folder:** the arrows (or Left / Right, Page Up / Page Down, Space / Backspace, Home / End) go through the pictures of the same folder in natural order (`img2` before `img10`).
- **Zoom:** mouse wheel (towards the pointer), `+` / `-`, `0` fits the window, `1` is 100%, `2` is 200%; the chip at the top left goes between "fit" and 100%. Drag the picture to move it (right or middle button always drags, even while drawing).
- **Turn and mirror:** `R` turns clockwise (Shift+R the other way), `H` mirrors. They are part of the edit: Save writes the turned picture.
- **Full screen:** `F` / `F11` or a double click on the picture; Esc leaves it.
- **Copy:** Ctrl+C puts the picture (with what you drew) on the clipboard as a PNG (needs `wl-clipboard`). **Trash:** the Delete key or the button (`gio trash`: it can be restored).

## Drawing

Press `E` (or the pencil button) to open the tools: undo, redo, mirror, then pen, marker (translucent), arrow, rectangle, ellipse, text and pixelate; seven colours (the first is the primary colour of your shell) and three thicknesses. The thickness is relative to the size of the picture, so it looks the same on a thumbnail and on a 40 MP photo. In edit mode the keys `3` to `9` pick the tools.

- **Text:** click where it goes and type; Enter makes a new line, Esc or Ctrl+Enter finishes (a click elsewhere too). Ctrl+V pastes.
- **Pixelate:** drag a rectangle over what you want to hide; the area becomes coarse blocks in the saved file.
- Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) undo and redo.

## Saving

Saving **never replaces the original**: Ctrl+S (or the disk button) writes a copy beside it, `name-edited.png` (`name-edited 2.png` if that exists; a JPEG stays a JPEG, everything else is saved as PNG), through a temporary file, so a failure never leaves half a file, and an existing file is never overwritten by it. Ctrl+Shift+S asks for a name (and a confirmation if you pick an existing one). After saving, the saved picture is the one shown. If you leave with unsaved changes it asks whether to save a copy, discard or cancel.
