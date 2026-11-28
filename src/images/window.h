// What Imag3s does and its Wayland window: opening and loading pictures, the folder, the view, the edit, saving, the keys, the mouse, the loop.
// Included once by images.cpp, inside its anonymous namespace (after ui.h).
#pragma once

void say(const std::string& msg) { g.toast = msg; g.toastAt = Clock::now(); g.dirty = true; }
void wake() { const char c = 1; if (g_wakeFd >= 0 && write(g_wakeFd, &c, 1) < 0) {} }

// ---- the pictures ----------------------------------------------------------------------------------------------------------------------
void findExtensions() {
    for (GSList* l = gdk_pixbuf_get_formats(); l; l = l->next) {
        auto* fmt = static_cast<GdkPixbufFormat*>(l->data);
        if (gdk_pixbuf_format_is_disabled(fmt)) continue;
        gchar** ext = gdk_pixbuf_format_get_extensions(fmt);
        for (gchar** e = ext; e && *e; e++) g.extensions.insert(*e);
        g_strfreev(ext);
    }
    // (a loader may be missing; the rest are not reachable by name anyway)
}
std::string lowerExt(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    if (!e.empty()) e.erase(0, 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}
bool isPicturePath(const std::string& path) { return g.extensions.count(lowerExt(path)) > 0; }

void listSiblings(const std::string& path) {
    const std::string dir = fs::path(path).parent_path().string();
    if (dir != g.siblingsDir || std::find(g.siblings.begin(), g.siblings.end(), path) == g.siblings.end()) {
        g.siblings.clear();
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string p = it->path().string();
            if (!isPicturePath(p)) continue;
            std::error_code e2;
            if (it->is_regular_file(e2)) g.siblings.push_back(p);
        }
        std::sort(g.siblings.begin(), g.siblings.end(), [](const std::string& a, const std::string& b) { return naturalLess(fs::path(a).filename().string(), fs::path(b).filename().string()); });
        g.siblingsDir = dir;
    }
    const auto it = std::find(g.siblings.begin(), g.siblings.end(), path);
    g.index = it == g.siblings.end() ? -1 : static_cast<int>(it - g.siblings.begin());
}

// A pixbuf as the pixels cairo paints (premultiplied), or nullptr when it is too big for memory.
cairo_surface_t* pixbufToSurface(GdkPixbuf* pb) {
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(img); return nullptr; }
    unsigned char* dst = cairo_image_surface_get_data(img);
    const int drs = cairo_image_surface_get_stride(img);
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            const guchar* q = px + static_cast<size_t>(y) * rs + static_cast<size_t>(x) * nch;
            const unsigned a = nch == 4 ? q[3] : 255;
            *reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * drs + static_cast<size_t>(x) * 4) =
                (a << 24) | (((q[0] * a + 127) / 255) << 16) | (((q[1] * a + 127) / 255) << 8) | ((q[2] * a + 127) / 255);
        }
    cairo_surface_mark_dirty(img);
    return img;
}

// Reads a picture in a thread (a big photo takes a moment): the result waits in g.loaded for the loop. A GIF or WebP that moves comes back with
// its animation (the surface is its first frame).
cairo_surface_t* decodePicture(const std::string& path, uint64_t& bytes, std::string& error, GdkPixbufAnimation*& anim) {
    anim = nullptr;
    int w = 0, h = 0;
    if (!gdk_pixbuf_get_file_info(path.c_str(), &w, &h)) { error = i18n::tr("This file is not a picture that can be shown"); return nullptr; }
    if (double(w) * double(h) > kMaxPixels) { error = i18n::tr("This picture is too big to open"); return nullptr; }
    struct stat st{};
    bytes = stat(path.c_str(), &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
    const std::string ext = lowerExt(path);
    GError* err = nullptr;
    if ((ext == "gif" || ext == "webp") && bytes < 96u * 1024 * 1024 && double(w) * double(h) < 40e6) {
        GdkPixbufAnimation* a = gdk_pixbuf_animation_new_from_file(path.c_str(), &err);
        if (err) { g_error_free(err); err = nullptr; }
        if (a && !gdk_pixbuf_animation_is_static_image(a)) {
            GdkPixbufAnimationIter* it = gdk_pixbuf_animation_get_iter(a, nullptr);
            cairo_surface_t* first = it ? pixbufToSurface(gdk_pixbuf_animation_iter_get_pixbuf(it)) : nullptr;
            if (it) g_object_unref(it);
            if (first) { anim = a; return first; }
        }
        if (a) g_object_unref(a);
    }
    GdkPixbuf* raw = gdk_pixbuf_new_from_file(path.c_str(), &err);
    if (!raw) { if (err) g_error_free(err); error = i18n::tr("This file is not a picture that can be shown"); return nullptr; }
    GdkPixbuf* pb = gdk_pixbuf_apply_embedded_orientation(raw);  // (photos taken sideways)
    g_object_unref(raw);
    if (!pb) { error = i18n::tr("This file is not a picture that can be shown"); return nullptr; }
    cairo_surface_t* img = pixbufToSurface(pb);
    g_object_unref(pb);
    if (!img) error = i18n::tr("This picture is too big to open");
    return img;
}

void startLoad(const std::string& path) {
    const uint64_t serial = ++g.loadSerial;
    g.loading = true;
    g.dirty = true;
    std::thread([path, serial] {
        Loaded out;
        out.serial = serial;
        out.path = path;
        out.surface = decodePicture(path, out.bytes, out.error, out.anim);
        {
            std::lock_guard<std::mutex> lock(g.loadedMutex);
            g.loaded.push_back(std::move(out));
        }
        wake();
    }).detach();
}

void updateTitle() {
    if (!g.toplevel) return;
    const std::string t = (g.path.empty() ? std::string() : shortName() + " — ") + "Imag3s";
    xdg_toplevel_set_title(g.toplevel, t.c_str());
}

void stopAnimation() {
    if (g.animIter) g_object_unref(g.animIter);
    if (g.anim) g_object_unref(g.anim);
    g.animIter = nullptr;
    g.anim = nullptr;
}
// When the next frame of a moving picture is due.
void scheduleFrame() {
    if (!g.animIter) return;
    const int ms = gdk_pixbuf_animation_iter_get_delay_time(g.animIter);
    g.nextFrame = Clock::now() + std::chrono::milliseconds(ms < 0 ? 100000 : std::max(20, ms));
}
// It moves only while nothing is being drawn on it (an edit is made on the frame that was showing).
bool animating() { return g.anim && g.animIter && g.animPlaying && !g.editing && !g.edits.dirty(); }
void stepAnimation() {
    if (!animating() || Clock::now() < g.nextFrame) return;
    gdk_pixbuf_animation_iter_advance(g.animIter, nullptr);
    if (cairo_surface_t* frame = pixbufToSurface(gdk_pixbuf_animation_iter_get_pixbuf(g.animIter))) {
        if (g.image) cairo_surface_destroy(g.image);
        g.image = frame;
        g.dirty = true;
    }
    scheduleFrame();
}

void applyLoaded() {
    std::vector<Loaded> done;
    {
        std::lock_guard<std::mutex> lock(g.loadedMutex);
        done.swap(g.loaded);
    }
    for (Loaded& l : done) {
        if (l.serial != g.loadSerial.load()) { if (l.surface) cairo_surface_destroy(l.surface); if (l.anim) g_object_unref(l.anim); continue; }  // (an older request: the user already moved on)
        g.loading = false;
        if (!l.surface) { say(fs::path(l.path).filename().string() + ": " + l.error); continue; }
        if (g.image) cairo_surface_destroy(g.image);
        g.image = l.surface;
        stopAnimation();
        if (l.anim) {  // it moves: from the first frame, at the pace its file says
            g.anim = l.anim;
            g.animIter = gdk_pixbuf_animation_get_iter(g.anim, nullptr);
            g.animPlaying = true;
            scheduleFrame();
        }
        g.path = l.path;
        g.bytes = l.bytes;
        g.edits.clear();
        g.drawing = g.typing = false;
        zoomFit();
        listSiblings(g.path);
        updateTitle();
        g.dirty = true;
    }
}

// ---- running programs: the dialogs and the clipboard -----------------------------------------------------------------------------------
void startDialogThread(std::vector<std::string> argv, bool saving) {
    std::thread([argv = std::move(argv), saving] {
        bool ok = false;
        std::string out = runCapture(argv, 0, ok);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        {
            std::lock_guard<std::mutex> lock(g.chosenMutex);
            if (ok && !out.empty()) { if (saving) g.chosenSave = out; else g.chosenOpen.push_back(out); }
            else if (saving) g.chosenSave = "\x01";  // cancelled
        }
        wake();
    }).detach();
}
void chooseFile() {
    std::string filter = "--file-filter=";
    filter += i18n::tr("Pictures");
    filter += " |";
    for (const std::string& e : g.extensions) filter += " *." + e + " *." + [&] { std::string u = e; for (char& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return u; }();
    startDialogThread({"zenity", "--file-selection", filter, "--title=Imag3s"}, false);
}

void copyPicture() {
    if (!g.image) return;
    cairo_surface_t* flat = flatten(g.image, g.edits);
    const std::string png = pngBytes(flat);
    cairo_surface_destroy(flat);
    if (png.empty()) { say(i18n::tr("The picture could not be copied")); return; }
    say(runWrite({"wl-copy", "--type", "image/png"}, png) ? i18n::tr("Picture copied") : i18n::tr("The picture could not be copied (is wl-clipboard installed?)"));
}

// ---- saving: always a new file (the original is kept), through a temporary file ----------------------------------------------------------
std::string copyPathFor(const std::string& original) {
    const fs::path p(original);
    std::string ext = lowerExt(original);
    ext = ext == "jpg" || ext == "jpeg" ? ext : "png";
    const std::string base = (p.parent_path() / p.stem()).string() + "-edited";
    std::string out = base + "." + ext;
    for (int n = 2; fs::exists(out) && n < 1000; n++) out = base + " " + std::to_string(n) + "." + ext;
    return out;
}
bool writePicture(cairo_surface_t* s, std::string out, bool overwrite, std::string& finalPath, std::string& error) {
    const std::string ext = lowerExt(out);
    const bool jpeg = ext == "jpg" || ext == "jpeg";
    if (!jpeg && ext != "png") out += ".png";
    const std::string tmp = out + ".part";
    bool ok = false;
    if (jpeg) ok = saveJpeg(s, tmp);
    else ok = cairo_surface_write_to_png(s, tmp.c_str()) == CAIRO_STATUS_SUCCESS;
    if (!ok) { unlink(tmp.c_str()); error = i18n::tr("The picture could not be written here"); return false; }
    if (overwrite) ok = rename(tmp.c_str(), out.c_str()) == 0;
    else { ok = link(tmp.c_str(), out.c_str()) == 0; unlink(tmp.c_str()); }  // (link fails when the name appeared meanwhile: nothing is replaced)
    if (!ok) { unlink(tmp.c_str()); error = i18n::tr("The picture could not be written here"); return false; }
    finalPath = out;
    return true;
}

void finishAfter(const std::string& saved);
bool saveTo(const std::string& out, bool overwrite) {
    if (!g.image) return false;
    cairo_surface_t* flat = flatten(g.image, g.edits);
    std::string finalPath, error;
    const bool ok = writePicture(flat, out, overwrite, finalPath, error);
    cairo_surface_destroy(flat);
    if (!ok) { say(error); g.after = After::Nothing; return false; }
    say(std::string(i18n::tr("Saved")) + ": " + fs::path(finalPath).filename().string());
    finishAfter(finalPath);
    return true;
}
void saveCopy() { if (g.image) saveTo(copyPathFor(g.path), false); }
void saveCopyAs() {
    if (!g.image) return;
    startDialogThread({"zenity", "--file-selection", "--save", "--confirm-overwrite", "--filename=" + copyPathFor(g.path), "--title=Imag3s"}, true);
}

// ---- switching away from edits ---------------------------------------------------------------------------------------------------------
void commitText() {
    if (!g.typing) return;
    g.typing = false;
    if (!g.edits.marks.empty() && g.edits.marks.back().tool == im::Tool::Text && g.edits.marks.back().text.empty()) g.edits.marks.pop_back();
    g.dirty = true;
}
void requestLoad(const std::string& path) {
    commitText();
    if (g.image && g.edits.dirty()) { g.dialog = true; g.after = After::Load; g.afterPath = path; g.dirty = true; return; }
    startLoad(path);
}
void requestQuit() {
    commitText();
    if (g.image && g.edits.dirty()) { g.dialog = true; g.after = After::Quit; g.dirty = true; return; }
    g.running = false;
}
// What was waiting for the edits to be dealt with (after a save or a discard).
void runAfter(const std::string& saved) {
    const After what = g.after;
    g.after = After::Nothing;
    g.dialog = false;
    if (what == After::Quit) g.running = false;
    else if (what == After::Load) startLoad(g.afterPath);
    else if (!saved.empty()) startLoad(saved);  // a plain save shows the picture that was written
}
void finishAfter(const std::string& saved) { runAfter(saved); }

void openPath(const std::string& given) {
    char real[4096];
    const std::string path = realpath(given.c_str(), real) ? std::string(real) : given;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) { say(fs::path(given).filename().string() + ": " + i18n::tr("This file is not a picture that can be shown")); return; }
    requestLoad(path);
}
void navigate(int delta) {
    if (g.siblings.size() < 2 || g.index < 0) return;
    const int n = static_cast<int>(g.siblings.size());
    requestLoad(g.siblings[static_cast<size_t>(((g.index + delta) % n + n) % n)]);
}
void trashPicture() {
    if (g.path.empty()) return;
    bool ok = false;
    runCapture({"gio", "trash", "--", g.path}, 8000, ok);
    if (!ok) { say(i18n::tr("The picture could not be moved to the Trash")); return; }
    const std::string gone = g.path;
    const auto it = std::find(g.siblings.begin(), g.siblings.end(), gone);
    int next = -1;
    if (it != g.siblings.end()) {
        const int at = static_cast<int>(it - g.siblings.begin());
        g.siblings.erase(it);
        if (!g.siblings.empty()) next = std::min(at, static_cast<int>(g.siblings.size()) - 1);
    }
    say(std::string(i18n::tr("Moved to the Trash")) + ": " + fs::path(gone).filename().string());
    g.edits.clear();
    if (next >= 0) startLoad(g.siblings[static_cast<size_t>(next)]);
    else { if (g.image) cairo_surface_destroy(g.image); g.image = nullptr; g.path.clear(); g.index = -1; updateTitle(); }
    g.dirty = true;
}

void toggleFullscreen() {
    g.fullscreen = !g.fullscreen;
    if (g.fullscreen) xdg_toplevel_set_fullscreen(g.toplevel, nullptr);
    else xdg_toplevel_unset_fullscreen(g.toplevel);
    g.dirty = true;
}
void setEditing(bool on) {
    commitText();
    if (!on) scheduleFrame();
    g.editing = on && g.image;
    g.drawing = false;
    g.dirty = true;
}
void startMark(double x, double y) {
    const im::Pt p = toImage(x, y);
    const im::Pt s = imageSize();
    im::Mark m;
    m.tool = g.tool;
    const Color c = paletteColor(g.colorIdx);
    m.rgb[0] = c.r; m.rgb[1] = c.g; m.rgb[2] = c.b;
    m.width = im::strokeWidth(g.widthStep, s.x, s.y);
    m.pts.push_back(p);
    if (g.tool == im::Tool::Text) {
        m.width = std::max(14.0, m.width * 5);
        g.edits.add(m);
        g.typing = true;
        g.caretReset = Clock::now();
        g.dirty = true;
        return;
    }
    if (g.tool != im::Tool::Pen && g.tool != im::Tool::Marker) m.pts.push_back(p);
    g.current = m;
    g.drawing = true;
    g.dirty = true;
}
void growMark(double x, double y) {
    const im::Pt p = toImage(x, y);
    im::Mark& m = g.current;
    if (m.tool == im::Tool::Pen || m.tool == im::Tool::Marker) {
        const im::Pt last = m.pts.back();
        if (std::hypot(p.x - last.x, p.y - last.y) * g.zoom >= 1.5) m.pts.push_back(p);
    } else m.pts.back() = p;
    g.dirty = true;
}
void finishMark() {
    if (!g.drawing) return;
    g.drawing = false;
    const im::Mark& m = g.current;
    const im::Pt a = m.pts.front(), b = m.pts.back();
    const bool shape = m.tool != im::Tool::Pen && m.tool != im::Tool::Marker;
    if (!shape || std::hypot(b.x - a.x, b.y - a.y) * g.zoom >= 4) g.edits.add(m);
    g.dirty = true;
}

// ---- the keys --------------------------------------------------------------------------------------------------------------------------
bool modActive(const char* name) { return g.xkbState && xkb_state_mod_name_is_active(g.xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0; }

std::string keyText(uint32_t key, xkb_keysym_t sym, bool& consumed) {
    consumed = false;
    if (g.composeState && !modActive(XKB_MOD_NAME_CTRL) && !modActive(XKB_MOD_NAME_ALT)) {
        xkb_compose_state_feed(g.composeState, sym);
        switch (xkb_compose_state_get_status(g.composeState)) {
            case XKB_COMPOSE_COMPOSING: consumed = true; return "";
            case XKB_COMPOSE_CANCELLED: xkb_compose_state_reset(g.composeState); consumed = true; return "";
            case XKB_COMPOSE_COMPOSED: {
                char buf[64];
                const int n = xkb_compose_state_get_utf8(g.composeState, buf, sizeof buf);
                xkb_compose_state_reset(g.composeState);
                return n > 0 ? std::string(buf, static_cast<size_t>(n)) : "";
            }
            default: break;
        }
    }
    char buf[16];
    const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f) return "";
    return std::string(buf, static_cast<size_t>(n));
}

void undo() { commitText(); if (g.edits.undo()) g.dirty = true; }
void redo() { if (g.edits.redo()) g.dirty = true; }
void turn(int q) { commitText(); g.edits.turn(q); if (g.fit) zoomFit(); else clampPan(); g.dirty = true; }
void mirror() { commitText(); g.edits.mirror(); g.dirty = true; }

// While a text is being typed. True when the key can repeat.
bool typingKey(uint32_t key, xkb_keysym_t sym, bool ctrl) {
    im::Mark& m = g.edits.marks.back();
    g.caretReset = Clock::now();
    g.dirty = true;
    switch (sym) {
        case XKB_KEY_Escape: commitText(); return false;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter:
            if (ctrl) { commitText(); return false; }
            m.text += '\n';
            return false;
        case XKB_KEY_BackSpace: {
            if (m.text.empty()) return true;
            size_t n = m.text.size() - 1;
            while (n > 0 && (static_cast<unsigned char>(m.text[n]) & 0xC0) == 0x80) n--;
            m.text.erase(n);
            return true;
        }
        case XKB_KEY_z: case XKB_KEY_Z: if (ctrl) { undo(); return false; } break;
        case XKB_KEY_v: case XKB_KEY_V:
            if (ctrl) {
                bool ok = false;
                std::string p = runCapture({"wl-paste", "--no-newline"}, 800, ok);
                if (ok && p.size() < 2000) m.text += p;
                return false;
            }
            break;
        default: break;
    }
    if (ctrl) return false;
    bool consumed;
    const std::string t = keyText(key, sym, consumed);
    if (t.empty() || m.text.size() > 2000) return consumed;
    m.text += t;
    return true;
}

bool handleKey(uint32_t key) {
    if (!g.xkbState) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, key + 8);
    const bool ctrl = modActive(XKB_MOD_NAME_CTRL), shift = modActive(XKB_MOD_NAME_SHIFT);
    g.dirty = true;
    if (g.dialog) {
        if (sym == XKB_KEY_Escape) { g.dialog = false; g.after = After::Nothing; }
        else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) saveCopy();
        return false;
    }
    if (g.typing) return typingKey(key, sym, ctrl);
    if (ctrl) {
        switch (sym) {
            case XKB_KEY_z: case XKB_KEY_Z: if (shift) redo(); else undo(); return false;
            case XKB_KEY_y: case XKB_KEY_Y: redo(); return false;
            case XKB_KEY_s: case XKB_KEY_S: if (shift) saveCopyAs(); else saveCopy(); return false;
            case XKB_KEY_c: case XKB_KEY_C: copyPicture(); return false;
            case XKB_KEY_o: case XKB_KEY_O: chooseFile(); return false;
            case XKB_KEY_q: case XKB_KEY_Q: case XKB_KEY_w: case XKB_KEY_W: requestQuit(); return false;
            case XKB_KEY_plus: case XKB_KEY_equal: case XKB_KEY_KP_Add: zoomAt(g.zoom * 1.25, g.canvas.x + g.canvas.w / 2, g.canvas.y + g.canvas.h / 2); return true;
            case XKB_KEY_minus: case XKB_KEY_KP_Subtract: zoomAt(g.zoom / 1.25, g.canvas.x + g.canvas.w / 2, g.canvas.y + g.canvas.h / 2); return true;
            case XKB_KEY_0: zoomFit(); return false;
            default: return false;
        }
    }
    const double cx = g.canvas.x + g.canvas.w / 2, cy = g.canvas.y + g.canvas.h / 2;
    switch (sym) {
        case XKB_KEY_Escape:
            if (g.fullscreen) toggleFullscreen();
            else if (g.editing) setEditing(false);
            else requestQuit();
            return false;
        case XKB_KEY_Left: case XKB_KEY_Page_Up: case XKB_KEY_BackSpace: navigate(-1); return true;
        case XKB_KEY_Right: case XKB_KEY_Page_Down: case XKB_KEY_space: navigate(1); return true;
        case XKB_KEY_Home: if (!g.siblings.empty()) requestLoad(g.siblings.front()); return false;
        case XKB_KEY_End: if (!g.siblings.empty()) requestLoad(g.siblings.back()); return false;
        case XKB_KEY_plus: case XKB_KEY_equal: case XKB_KEY_KP_Add: zoomAt(g.zoom * 1.25, cx, cy); return true;
        case XKB_KEY_minus: case XKB_KEY_KP_Subtract: zoomAt(g.zoom / 1.25, cx, cy); return true;
        case XKB_KEY_0: zoomFit(); return false;
        case XKB_KEY_1: zoomAt(1.0, cx, cy); return false;
        case XKB_KEY_2: zoomAt(2.0, cx, cy); return false;
        case XKB_KEY_F11: case XKB_KEY_f: case XKB_KEY_F: toggleFullscreen(); return false;
        case XKB_KEY_r: case XKB_KEY_R: turn(shift ? -1 : 1); return false;
        case XKB_KEY_h: case XKB_KEY_H: mirror(); return false;
        case XKB_KEY_e: case XKB_KEY_E: setEditing(!g.editing); return false;
        case XKB_KEY_Delete: trashPicture(); return false;
        case XKB_KEY_Up: if (!g.fit) { g.panY += 60; clampPan(); } return true;
        case XKB_KEY_Down: if (!g.fit) { g.panY -= 60; clampPan(); } return true;
        default: break;
    }
    if (g.editing) {  // the tools by number
        if (sym >= XKB_KEY_3 && sym <= XKB_KEY_9) { g.tool = kTools[sym - XKB_KEY_3]; return false; }
    }
    return false;
}

// ---- the mouse -------------------------------------------------------------------------------------------------------------------------
void setHoverImpl() {
    int id = None, arg = 0;
    if (g.inside) for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it) if (it->r.contains(g.px, g.py)) { id = it->id; arg = it->arg; break; }
    if (id != g.hoverId || arg != g.hoverArg) { g.hoverId = id; g.hoverArg = arg; g.dirty = true; }
}
void updateCursor();
void setHover() { setHoverImpl(); updateCursor(); }

void updateCursor() {
    if (!g.cursorMgr || !g.pointer || !g.inside) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    int shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (g.panning) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING;
    else if (g.hoverId == Canvas && g.image) {
        if (g.editing) shape = g.tool == im::Tool::Text ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
        else shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
    } else if (g.hoverId != None && g.hoverId != Scrim) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    if (shape == g.cursorShape) return;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}

void onMotion() {
    if (g.panning) { g.panX = g.panStartX + (g.px - g.panFromX); g.panY = g.panStartY + (g.py - g.panFromY); g.fit = false; clampPan(); g.dirty = true; return; }
    if (g.drawing) { growMark(g.px, g.py); return; }
    setHover();
}
void beginPan() {
    g.panning = true;
    g.panFromX = g.px; g.panFromY = g.py; g.panStartX = g.panX; g.panStartY = g.panY;
    updateCursor();
}
void onRelease(uint32_t button) {
    if (button == 0x110) finishMark();
    if (g.panning) { g.panning = false; setHover(); }
}

void onClick(uint32_t button) {
    const int id = g.hoverId, arg = g.hoverArg;
    if (g.dialog) {
        if (button != 0x110) return;
        if (id == DlgSave) saveCopy();
        else if (id == DlgDiscard) { g.edits.clear(); runAfter(""); }
        else if (id == DlgCancel) { g.dialog = false; g.after = After::Nothing; }
        g.dirty = true;
        return;
    }
    if (id == Canvas && g.image) {
        if (button == 0x111 || button == 0x112) { commitText(); beginPan(); return; }  // right or middle button: always drag the picture
        if (button != 0x110) return;
        commitText();
        if (g.editing) { startMark(g.px, g.py); return; }
        const auto now = Clock::now();
        g.clickCount = std::chrono::duration<double>(now - g.lastClick).count() < 0.4 ? g.clickCount + 1 : 1;
        g.lastClick = now;
        if (g.clickCount >= 2) { g.clickCount = 0; toggleFullscreen(); return; }
        beginPan();
        return;
    }
    if (button != 0x110) return;
    switch (id) {
        case BtnOpen: chooseFile(); break;
        case BtnPrev: case NavLeft: navigate(-1); break;
        case BtnNext: case NavRight: navigate(1); break;
        case BtnEdit: setEditing(!g.editing); break;
        case BtnRotate: turn(1); break;
        case BtnCopy: copyPicture(); break;
        case BtnTrash: trashPicture(); break;
        case BtnSave: saveCopy(); break;
        case BtnClose: requestQuit(); break;
        case AnimBtn: g.animPlaying = !g.animPlaying; scheduleFrame(); break;
        case ZoomChip: if (g.fit || std::fabs(g.zoom - 1) > 0.01) zoomAt(1.0, g.canvas.x + g.canvas.w / 2, g.canvas.y + g.canvas.h / 2); else zoomFit(); break;
        case ToolBtn: commitText(); g.tool = static_cast<im::Tool>(arg); break;
        case ColorBtn: g.colorIdx = arg; break;
        case WidthBtn: g.widthStep = arg; break;
        case BtnUndo: undo(); break;
        case BtnRedo: redo(); break;
        case BtnFlip: mirror(); break;
        default: return;
    }
    g.dirty = true;
}

void onWheel(double v) {
    if (g.dialog || !g.image) return;
    zoomAt(g.zoom * std::pow(1.1, -v / 10.0), g.px, g.py);
}

// ---- Wayland ---------------------------------------------------------------------------------------------------------------------------
void onFrameDone(void*, wl_callback* cb, uint32_t) { wl_callback_destroy(cb); g.frameCallback = nullptr; }
const wl_callback_listener kFrameListener = {onFrameDone};
void onBufferRelease(void* data, wl_buffer*) { static_cast<Buffer*>(data)->busy = false; }
const wl_buffer_listener kBufferListener = {onBufferRelease};

Buffer* acquireBuffer(int width, int height, double scale) {
    Buffer* spare = nullptr;
    for (auto& slot : g.buffers) {
        if (slot.busy) continue;
        if (slot.buffer && slot.frame.lw == width && slot.frame.lh == height && slot.frame.scale == scale) return &slot;
        if (!spare) spare = &slot;
    }
    if (!spare) return nullptr;
    spare->clear();
    auto& frame = spare->frame;
    if (!hidpi::begin(frame, width, height, scale, "end4pC-imag3s")) return nullptr;
    wl_shm_pool* pool = wl_shm_create_pool(g.shm, frame.fd, static_cast<int32_t>(frame.size));
    if (pool) {
        spare->buffer = wl_shm_pool_create_buffer(pool, 0, frame.pw, frame.ph, frame.stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
    }
    close(frame.fd);
    frame.fd = -1;
    if (!spare->buffer) { spare->clear(); return nullptr; }
    wl_buffer_add_listener(spare->buffer, &kBufferListener, spare);
    return spare;
}

void render() {
    if (!g.configured || !g.surface || g.frameCallback) return;
    Buffer* slot = acquireBuffer(g.width, g.height, g.scale.factor());
    if (!slot) return;
    g_scale = g.scale.factor();
    hidpi::Frame& f = slot->frame;
    cairo_t* cr = mkcr(f.surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    drawWindow(cr, g.width, g.height);
    cairo_destroy(cr);
    cairo_surface_flush(f.surface);
    g.frameCallback = wl_surface_frame(g.surface);
    if (g.frameCallback) wl_callback_add_listener(g.frameCallback, &kFrameListener, nullptr);
    slot->busy = true;
    hidpi::present(g.surface, slot->buffer, f, g.scale);
    g.dirty = false;
    setHover();
}

void onWmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmListener = {onWmPing};
void onXdgConfigure(void*, xdg_surface* s, uint32_t serial) { xdg_surface_ack_configure(s, serial); g.configured = true; g.dirty = true; }
const xdg_surface_listener kXdgSurfaceListener = {onXdgConfigure};
void onTopConfigure(void*, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
    if (w > 0 && h > 0) { g.width = std::max(kMinW, w); g.height = std::max(kMinH, h); }
    g.dirty = true;
}
void onTopClose(void*, xdg_toplevel*) { requestQuit(); }
const xdg_toplevel_listener kToplevelListener = {onTopConfigure, onTopClose, nullptr, nullptr};

void onPEnter(void*, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) { g.enterSerial = serial; g.cursorShape = -1; g.inside = true; g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); setHover(); }
void onPLeave(void*, wl_pointer*, uint32_t, wl_surface*) { g.inside = false; setHover(); }
void onPMotion(void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) { g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); onMotion(); }
void onPButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) { if (state == WL_POINTER_BUTTON_STATE_PRESSED) onClick(button); else onRelease(button); }
void onPAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) { if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) onWheel(wl_fixed_to_double(value)); }
const wl_pointer_listener kPointerListener = {onPEnter, onPLeave, onPMotion, onPButton, onPAxis, [](void*, wl_pointer*) {}, [](void*, wl_pointer*, uint32_t) {},
                                              [](void*, wl_pointer*, uint32_t, uint32_t) {}, [](void*, wl_pointer*, uint32_t, int32_t) {}, nullptr, nullptr, nullptr};

void onKKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            if (!g.xkbCtx) g.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_keymap* km = xkb_keymap_new_from_string(g.xkbCtx, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (km) {
                if (g.xkbState) xkb_state_unref(g.xkbState);
                if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
                g.xkbMap = km;
                g.xkbState = xkb_state_new(km);
            }
        }
    }
    close(fd);
}
void onKKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) { if (key == g.repeatKey) g.repeatKey = 0; return; }
    if (handleKey(key) && g.kbRate > 0) { g.repeatKey = key; g.repeatAt = Clock::now() + std::chrono::milliseconds(g.kbDelay); }
    else g.repeatKey = 0;
}
void onKMods(void*, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) { if (g.xkbState) xkb_state_update_mask(g.xkbState, dep, lat, lock, 0, 0, group); }
void onKRepeat(void*, wl_keyboard*, int32_t rate, int32_t delay) { g.kbRate = rate; g.kbDelay = delay; }
const wl_keyboard_listener kKeyboardListener = {onKKeymap, [](void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}, [](void*, wl_keyboard*, uint32_t, wl_surface*) { g.repeatKey = 0; },
                                                onKKey, onKMods, onKRepeat};

void onSeatCaps(void*, wl_seat* seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g.pointer) { g.pointer = wl_seat_get_pointer(seat); wl_pointer_add_listener(g.pointer, &kPointerListener, nullptr); }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g.keyboard) { g.keyboard = wl_seat_get_keyboard(seat); wl_keyboard_add_listener(g.keyboard, &kKeyboardListener, nullptr); }
}
const wl_seat_listener kSeatListener = {onSeatCaps, [](void*, wl_seat*, const char*) {}};

void onGlobal(void*, wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
    if (!std::strcmp(iface, wl_compositor_interface.name)) g.compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    else if (!std::strcmp(iface, wl_shm_interface.name)) g.shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    else if (!std::strcmp(iface, wp_viewporter_interface.name) && !hidpi::disabled()) g.viewporter = static_cast<wp_viewporter*>(wl_registry_bind(reg, name, &wp_viewporter_interface, 1));
    else if (!std::strcmp(iface, wp_fractional_scale_manager_v1_interface.name) && !hidpi::disabled())
        g.fscaleMgr = static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1));
    else if (!std::strcmp(iface, wp_cursor_shape_manager_v1_interface.name)) g.cursorMgr = static_cast<wp_cursor_shape_manager_v1*>(wl_registry_bind(reg, name, &wp_cursor_shape_manager_v1_interface, 1));
    else if (!std::strcmp(iface, wl_seat_interface.name)) {
        g.seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(g.seat, &kSeatListener, nullptr);
    } else if (!std::strcmp(iface, xdg_wm_base_interface.name)) {
        g.wm = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(g.wm, &kWmListener, nullptr);
    }
}
const wl_registry_listener kRegistryListener = {onGlobal, [](void*, wl_registry*, uint32_t) {}};

bool connectWayland() {
    g.display = wl_display_connect(nullptr);
    if (!g.display) { std::fprintf(stderr, "end4pC-imag3s: no Wayland display\n"); return false; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    if (wl_display_roundtrip(g.display) < 0 || !g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-imag3s: missing Wayland globals\n"); return false; }
    wl_display_roundtrip(g.display);
    g.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(g.scale, g.surface, g.viewporter, g.fscaleMgr, [] { g.dirty = true; });
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, "Imag3s");
    xdg_toplevel_set_app_id(g.toplevel, "end4pC-imag3s");
    xdg_toplevel_set_min_size(g.toplevel, kMinW, kMinH);
    wl_surface_commit(g.surface);
    return true;
}

void tick() {
    const auto now = Clock::now();
    applyLoaded();
    stepAnimation();
    {  // what the file dialogs answered
        std::vector<std::string> opened;
        std::string saved;
        {
            std::lock_guard<std::mutex> lock(g.chosenMutex);
            opened.swap(g.chosenOpen);
            saved.swap(g.chosenSave);
        }
        for (const std::string& p : opened) openPath(p);
        if (!saved.empty()) {
            if (saved == "\x01") g.after = After::Nothing;
            else saveTo(saved, true);
            g.dirty = true;
        }
    }
    if (g.repeatKey && now >= g.repeatAt) {
        if (!handleKey(g.repeatKey)) g.repeatKey = 0;
        g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
    }
    if (mtimeOf(colorsPath()) != g_colorsStamp) { loadColors(); g.dirty = true; }
    if (!g.toast.empty()) g.dirty = true;
    if (g.typing) {  // the caret blinks (a key press keeps it on)
        const bool on = std::chrono::duration_cast<std::chrono::milliseconds>(now - g.caretReset).count() / 530 % 2 == 0;
        if (on != g.caretOn) { g.caretOn = on; g.dirty = true; }
    }
}

// The loop wakes when the next key repeat is due (a held key repeats from the loop).
int pollTimeout() {
    int t = g.typing ? 120 : !g.toast.empty() ? 100 : g.loading ? 100 : 300;
    if (animating()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(g.nextFrame - Clock::now()).count();
        t = std::min<int>(t, static_cast<int>(std::max<long long>(1, left)));
    }
    if (g.repeatKey) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(g.repeatAt - Clock::now()).count();
        t = std::min<int>(t, static_cast<int>(std::max<long long>(1, left)));
    }
    return t;
}

void teardown() {
    if (g.cursorDev) wp_cursor_shape_device_v1_destroy(g.cursorDev);
    if (g.frameCallback) wl_callback_destroy(g.frameCallback);
    if (g.toplevel) xdg_toplevel_destroy(g.toplevel);
    if (g.xsurface) xdg_surface_destroy(g.xsurface);
    hidpi::detach(g.scale);
    if (g.surface) wl_surface_destroy(g.surface);
    for (auto& b : g.buffers) b.clear();
    if (g.display) { wl_display_flush(g.display); wl_display_disconnect(g.display); }
    stopAnimation();
    if (g.image) cairo_surface_destroy(g.image);
    if (g.composeState) xkb_compose_state_unref(g.composeState);
    if (g.composeTable) xkb_compose_table_unref(g.composeTable);
    if (g.xkbState) xkb_state_unref(g.xkbState);
    if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
    if (g.xkbCtx) xkb_context_unref(g.xkbCtx);
}

volatile sig_atomic_t g_term = 0;
void onTermSignal(int) { g_term = 1; }

int run(int argc, char** argv) {
    std::string lang;
    const std::string cfg = readFile(configPath());
    jsonValue(cfg, "language", lang);
    std::string mat, style, scale;
    matsym::assetsDir() = assetsDir();
    matsym::enabled() = jsonValue(cfg, "materialIcons", mat) && mat == "1";
    if (jsonValue(cfg, "iconScale", scale) && !scale.empty()) g_iconScale = std::clamp(std::atoi(scale.c_str()), 60, 150);
    if (jsonValue(cfg, "iconStyle", style) && !style.empty()) g_iconStyle = style;
    i18n::load(lang, assetsDir(), dataDir() + "/i18n");
    applyShellFont(fontFamilyFromConfig(dataDir() + "/config.json"), fontWeightFromConfig(dataDir() + "/config.json", "fontWeightText", 450), fontWeightFromConfig(dataDir() + "/config.json", "fontWeightTitle", 550));
    loadColors();
    findExtensions();
    if (!connectWayland()) return 1;
    g.xkbCtx = g.xkbCtx ? g.xkbCtx : xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    {
        const char* loc = std::getenv("LC_ALL");
        if (!loc || !*loc) loc = std::getenv("LC_CTYPE");
        if (!loc || !*loc) loc = std::getenv("LANG");
        g.composeTable = xkb_compose_table_new_from_locale(g.xkbCtx, loc && *loc ? loc : "C", XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (g.composeTable) g.composeState = xkb_compose_state_new(g.composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
    }
    cairo_surface_t* dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* dcr = mkcr(dummy);
    g_measure = pango_cairo_create_layout(dcr);

    int wake[2];
    if (pipe2(wake, O_CLOEXEC | O_NONBLOCK) < 0) return 1;
    g_wakeFd = wake[1];
    if (argc > 1) openPath(argv[1]);  // (without one it waits: the pictures are opened from the window)

    std::signal(SIGTERM, onTermSignal);
    std::signal(SIGINT, onTermSignal);
    std::signal(SIGHUP, onTermSignal);
    std::signal(SIGPIPE, SIG_IGN);
    while (g.running && !g_term) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);
        pollfd fds[] = {{wl_display_get_fd(g.display), POLLIN, 0}, {wake[0], POLLIN, 0}};
        if (poll(fds, 2, pollTimeout()) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & POLLIN) {
            char junk[64];
            while (read(wake[0], junk, sizeof junk) > 0) {}
            g.dirty = true;
        }
        tick();
        if (g.dirty) render();
    }
    teardown();
    g_object_unref(g_measure);
    cairo_destroy(dcr);
    cairo_surface_destroy(dummy);
    return 0;
}
