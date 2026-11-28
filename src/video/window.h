// What Vid3os does and its Wayland window: mpv, the folder, the keys, the mouse, the loop. Included once by vid3os.cpp, inside its anonymous namespace.
#pragma once

void say(const std::string& msg) { g.toast = msg; g.toastAt = Clock::now(); g.dirty = true; }
void wake() { const char c = 1; if (g_wakeFd >= 0 && write(g_wakeFd, &c, 1) < 0) {} }
void touch() { g.lastActivity = Clock::now(); g.dirty = true; }

// ---- the settings kept between runs ----------------------------------------------------------------------------------------------------
std::string settingsPath() { return dataDir() + "/vid3os.txt"; }
void loadSettings() {
    std::ifstream f(settingsPath());
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("volume=", 0) == 0) g.volume = std::clamp(std::atof(line.c_str() + 7), 0.0, 130.0);
        else if (line.rfind("muted=", 0) == 0) g.muted = line == "muted=1";
        else if (line.rfind("autoplay=", 0) == 0) g.autoplay = line != "autoplay=0";
    }
}
void saveSettings() {
    std::error_code ec;
    fs::create_directories(dataDir(), ec);
    std::ofstream f(settingsPath(), std::ios::trunc);
    f << "volume=" << static_cast<int>(std::lround(g.volume)) << "\nmuted=" << (g.muted ? 1 : 0) << "\nautoplay=" << (g.autoplay ? 1 : 0) << "\n";
}

// ---- mpv -------------------------------------------------------------------------------------------------------------------------------
void cmd(std::initializer_list<const char*> args) {
    std::vector<const char*> v(args);
    v.push_back(nullptr);
    if (g.mpv) mpv_command_async(g.mpv, 0, v.data());
}
void setFlag(const char* name, bool on) { int v = on ? 1 : 0; if (g.mpv) mpv_set_property_async(g.mpv, 0, name, MPV_FORMAT_FLAG, &v); }
void setDouble(const char* name, double d) { if (g.mpv) mpv_set_property_async(g.mpv, 0, name, MPV_FORMAT_DOUBLE, &d); }

bool initMpv() {
    g.mpv = mpv_create();
    if (!g.mpv) return false;
    // It is a player of local files: no scripts, no youtube-dl, no config file, no keys or on-screen display of mpv's own.
    const char* opts[][2] = {{"vo", "libmpv"}, {"idle", "yes"}, {"keep-open", "yes"}, {"config", "no"}, {"load-scripts", "no"}, {"ytdl", "no"},
                             {"input-default-bindings", "no"}, {"input-vo-keyboard", "no"}, {"osc", "no"}, {"osd-level", "0"}, {"terminal", "no"},
                             {"msg-level", "all=error"}, {"hwdec", "auto-safe"}, {"volume-max", "130"}, {"audio-display", "no"}, {"sub-auto", "fuzzy"},
                             {"save-position-on-quit", "no"}, {"load-unsafe-playlists", "no"}, {"cache", "yes"}};
    for (auto& o : opts) mpv_set_option_string(g.mpv, o[0], o[1]);
    if (mpv_initialize(g.mpv) < 0) return false;
    mpv_set_wakeup_callback(g.mpv, [](void*) { wake(); }, nullptr);
    const char* api = MPV_RENDER_API_TYPE_SW;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(api)}, {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&g.rctx, g.mpv, params) < 0) return false;
    mpv_render_context_set_update_callback(g.rctx, [](void*) { wake(); }, nullptr);
    mpv_observe_property(g.mpv, 1, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g.mpv, 2, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g.mpv, 3, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(g.mpv, 4, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(g.mpv, 5, "aid", MPV_FORMAT_STRING);
    setDouble("volume", g.volume);
    setFlag("mute", g.muted);
    return true;
}

// ---- the folder ------------------------------------------------------------------------------------------------------------------------
bool isVideoPath(const std::string& path) {
    static const std::set<std::string> ext = {"mp4", "mkv", "webm", "avi", "mov", "m4v", "mpg", "mpeg", "ts", "m2ts", "wmv", "flv", "ogv", "3gp", "vob", "mts"};
    std::string e = fs::path(path).extension().string();
    if (!e.empty()) e.erase(0, 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext.count(e) > 0;
}
void listSiblings(const std::string& path) {
    const std::string dir = fs::path(path).parent_path().string();
    if (dir != g.siblingsDir || std::find(g.siblings.begin(), g.siblings.end(), path) == g.siblings.end()) {
        g.siblings.clear();
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string p = it->path().string();
            std::error_code e2;
            if (isVideoPath(p) && it->is_regular_file(e2)) g.siblings.push_back(p);
        }
        std::sort(g.siblings.begin(), g.siblings.end(), [](const std::string& a, const std::string& b) { return naturalLess(fs::path(a).filename().string(), fs::path(b).filename().string()); });
        g.siblingsDir = dir;
    }
    const auto it = std::find(g.siblings.begin(), g.siblings.end(), path);
    g.index = it == g.siblings.end() ? -1 : static_cast<int>(it - g.siblings.begin());
}

void updateTitle() {
    if (!g.toplevel) return;
    const std::string t = (g.path.empty() ? std::string() : titleOfFile() + " — ") + "Vid3os";
    xdg_toplevel_set_title(g.toplevel, t.c_str());
}

void play(const std::string& path) {
    g.path = path;
    g.loaded = true;
    g.hasVideo = false;
    g.ended = false;
    g.pos = 0;
    g.dur = 0;
    listSiblings(path);
    updateTitle();
    cmd({"loadfile", path.c_str(), "replace"});
    setFlag("pause", false);
    touch();
}
// Only files that are there (not addresses): the player opens what you chose in the window or gave on the command line.
void openPath(const std::string& given) {
    char real[4096];
    const std::string path = realpath(given.c_str(), real) ? std::string(real) : given;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) { say(fs::path(given).filename().string() + ": " + i18n::tr("This file cannot be played")); return; }
    play(path);
}
void navigate(int delta) {
    if (g.siblings.size() < 2 || g.index < 0) return;
    const int n = static_cast<int>(g.siblings.size());
    play(g.siblings[static_cast<size_t>(((g.index + delta) % n + n) % n)]);
}

void startOpenDialog() {
    std::thread([] {
        bool ok = false;
        std::string out = runCapture({"zenity", "--file-selection", "--title=Vid3os", std::string("--file-filter=") + i18n::tr("Videos") + " | *.mp4 *.mkv *.webm *.avi *.mov *.m4v *.mpg *.mpeg *.ts *.wmv *.flv *.ogv *.3gp *.MP4 *.MKV *.WEBM *.AVI *.MOV"}, 0, ok);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        if (ok && !out.empty()) { std::lock_guard<std::mutex> lock(g.chosenMutex); g.chosenOpen.push_back(out); }
        wake();
    }).detach();
}

// ---- what the player does ----------------------------------------------------------------------------------------------------------------
void togglePause() {
    if (!g.loaded) return;
    if (g.ended || (g.dur > 0 && g.pos >= g.dur - 0.2)) { cmd({"seek", "0", "absolute"}); setFlag("pause", false); g.ended = false; }
    else setFlag("pause", !g.paused);
    touch();
}
void seekBy(double seconds) { if (!g.loaded) return; char b[32]; std::snprintf(b, sizeof b, "%.2f", seconds); cmd({"seek", b, "relative"}); g.ended = false; touch(); }
void seekTo(double fraction, bool exact) {
    if (!g.loaded || g.dur <= 0) return;
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", fraction * g.dur);
    cmd({"seek", b, exact ? "absolute+exact" : "absolute+keyframes"});
    g.ended = false;
}
void setVolume(double v) {
    g.volume = std::clamp(v, 0.0, 130.0);
    if (g.muted && v > 0) { g.muted = false; setFlag("mute", false); }
    setDouble("volume", g.volume);
    say(std::string(i18n::tr("Volume")) + " " + std::to_string(static_cast<int>(std::lround(g.volume))) + "%");
    saveSettings();
    touch();
}
void toggleMute() { g.muted = !g.muted; setFlag("mute", g.muted); saveSettings(); touch(); }
void toggleAutoplay() { g.autoplay = !g.autoplay; saveSettings(); say(std::string(i18n::tr("Continuous playback")) + ": " + (g.autoplay ? i18n::tr("On") : i18n::tr("Off"))); touch(); }
void cycleSpeed() { g.speed = vid::nextSpeed(g.speed); setDouble("speed", g.speed); say(std::string(i18n::tr("Speed")) + " " + vid::speedLabel(g.speed)); touch(); }
// Goes to the next track of that kind ("sid" or "aid") and says which one it is.
void cycleTrack(const char* prop, const char* label) {
    if (!g.loaded || !g.mpv) return;
    cmd({"cycle", prop, "up"});
    // (what it says is read a moment later, when mpv has changed it: see tick())
    g.toast.clear();
    g.pendingTrack = prop;
    g.pendingTrackLabel = label;
    g.pendingTrackAt = Clock::now();
    touch();
}
void toggleFullscreen() {
    g.fullscreen = !g.fullscreen;
    if (g.fullscreen) xdg_toplevel_set_fullscreen(g.toplevel, nullptr);
    else xdg_toplevel_unset_fullscreen(g.toplevel);
    touch();
}

// A string property of mpv ("sid", "aid", "current-tracks/sub/lang"...), or "".
std::string prop(const char* name) {
    if (!g.mpv) return "";
    char* s = mpv_get_property_string(g.mpv, name);
    std::string out = s ? s : "";
    if (s) mpv_free(s);
    return out;
}
void sayTrack() {
    const std::string kind = g.pendingTrack == "sid" ? "sub" : "audio";
    const std::string id = prop(g.pendingTrack.c_str());
    std::string lang = prop(("current-tracks/" + kind + "/lang").c_str());
    std::string title = prop(("current-tracks/" + kind + "/title").c_str());
    if (id == "no" || id.empty()) say(std::string(g.pendingTrackLabel) + ": " + i18n::tr("Off"));
    else say(std::string(g.pendingTrackLabel) + ": " + (!title.empty() ? title : !lang.empty() ? lang : id));
    g.pendingTrack.clear();
}

void handleEvents() {
    while (g.mpv) {
        mpv_event* ev = mpv_wait_event(g.mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        switch (ev->event_id) {
            case MPV_EVENT_SHUTDOWN: g.running = false; break;
            case MPV_EVENT_FILE_LOADED:
                g.hasVideo = !prop("current-tracks/video/id").empty() && prop("current-tracks/video/albumart") != "yes";
                g.dirty = true;
                break;
            case MPV_EVENT_END_FILE: {
                auto* e = static_cast<mpv_event_end_file*>(ev->data);
                if (e && e->reason == MPV_END_FILE_REASON_ERROR) { say(i18n::tr("This file cannot be played")); g.loaded = false; g.hasVideo = false; g.path.clear(); updateTitle(); }
                g.dirty = true;
                break;
            }
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* p = static_cast<mpv_event_property*>(ev->data);
                if (!p || p->format == MPV_FORMAT_NONE) break;
                const std::string name = p->name;
                if (name == "time-pos" && p->format == MPV_FORMAT_DOUBLE) g.pos = *static_cast<double*>(p->data);
                else if (name == "duration" && p->format == MPV_FORMAT_DOUBLE) g.dur = *static_cast<double*>(p->data);
                else if (name == "pause" && p->format == MPV_FORMAT_FLAG) { g.paused = *static_cast<int*>(p->data) != 0; g.lastActivity = Clock::now(); }
                else if (name == "aid" && p->format == MPV_FORMAT_STRING) { const char* v = *static_cast<char**>(p->data); g.audioOff = !v || std::string(v) == "no"; }
                else if (name == "eof-reached" && p->format == MPV_FORMAT_FLAG) {
                    const bool eof = *static_cast<int*>(p->data) != 0;
                    if (eof && !g.ended) {
                        g.ended = true;
                        if (g.autoplay && g.siblings.size() > 1 && g.index >= 0 && g.index + 1 < static_cast<int>(g.siblings.size())) play(g.siblings[static_cast<size_t>(g.index) + 1]);  // (the next one of the folder)
                        else { g.paused = true; g.lastActivity = Clock::now(); }
                    }
                }
                g.dirty = true;
                break;
            }
            default: break;
        }
    }
}

// ---- the keys --------------------------------------------------------------------------------------------------------------------------
bool modActive(const char* name) { return g.xkbState && xkb_state_mod_name_is_active(g.xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0; }

bool handleKey(uint32_t key) {
    if (!g.xkbState) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, key + 8);
    const bool ctrl = modActive(XKB_MOD_NAME_CTRL), shift = modActive(XKB_MOD_NAME_SHIFT);
    touch();
    if (ctrl) {
        switch (sym) {
            case XKB_KEY_o: case XKB_KEY_O: startOpenDialog(); return false;
            case XKB_KEY_q: case XKB_KEY_Q: case XKB_KEY_w: case XKB_KEY_W: g.running = false; return false;
            default: return false;
        }
    }
    switch (sym) {
        case XKB_KEY_space: case XKB_KEY_k: case XKB_KEY_K: togglePause(); return false;
        case XKB_KEY_Left: seekBy(shift ? -30 : -5); return true;
        case XKB_KEY_Right: seekBy(shift ? 30 : 5); return true;
        case XKB_KEY_j: case XKB_KEY_J: seekBy(-10); return true;
        case XKB_KEY_l: case XKB_KEY_L: seekBy(10); return true;
        case XKB_KEY_Up: setVolume(g.volume + 5); return true;
        case XKB_KEY_Down: setVolume(g.volume - 5); return true;
        case XKB_KEY_m: case XKB_KEY_M: toggleMute(); return false;
        case XKB_KEY_c: case XKB_KEY_C: toggleAutoplay(); return false;
        case XKB_KEY_f: case XKB_KEY_F: case XKB_KEY_F11: toggleFullscreen(); return false;
        case XKB_KEY_Escape: if (g.fullscreen) toggleFullscreen(); return false;
        case XKB_KEY_q: case XKB_KEY_Q: g.running = false; return false;
        case XKB_KEY_n: case XKB_KEY_N: case XKB_KEY_Page_Down: navigate(1); return false;
        case XKB_KEY_p: case XKB_KEY_P: case XKB_KEY_Page_Up: navigate(-1); return false;
        case XKB_KEY_bracketright: g.speed = std::min(4.0, g.speed + 0.25); setDouble("speed", g.speed); say(std::string(i18n::tr("Speed")) + " " + vid::speedLabel(g.speed)); return true;
        case XKB_KEY_bracketleft: g.speed = std::max(0.25, g.speed - 0.25); setDouble("speed", g.speed); say(std::string(i18n::tr("Speed")) + " " + vid::speedLabel(g.speed)); return true;
        case XKB_KEY_s: case XKB_KEY_S: cycleTrack("sid", i18n::tr("Subtitles")); return false;
        case XKB_KEY_a: case XKB_KEY_A: cycleTrack("aid", i18n::tr("Audio")); return false;
        case XKB_KEY_Home: seekTo(0, true); return false;
        default: return false;
    }
}

// ---- the mouse -------------------------------------------------------------------------------------------------------------------------
void setHoverImpl() {
    int id = None;
    if (g.inside) for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it) if (it->r.contains(g.px, g.py)) { id = it->id; break; }
    if (id != g.hoverId) { g.hoverId = id; g.dirty = true; }
}
void updateCursor() {
    if (!g.cursorMgr || !g.pointer || !g.inside) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    const bool hide = g.loaded && !g.paused && !controlsShown();
    if (hide) {  // the pointer goes away with the controls
        if (!g.cursorHidden) { wl_pointer_set_cursor(g.pointer, g.enterSerial, nullptr, 0, 0); g.cursorHidden = true; }
        return;
    }
    int shape = g.hoverId == None || g.hoverId == Canvas ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    if (shape == g.cursorShape && !g.cursorHidden) return;
    g.cursorHidden = false;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}
void setHover() { setHoverImpl(); updateCursor(); }

void dragSeek() {
    g.seekFraction = vid::barFraction(g.px, g.seekBar.x, g.seekBar.w);
    const auto now = Clock::now();
    if (now - g.lastSeek > std::chrono::milliseconds(90)) { seekTo(g.seekFraction, false); g.lastSeek = now; }  // (while dragging the picture follows)
    g.dirty = true;
}
void dragVolume() { setVolume(vid::barFraction(g.px, g.volumeBar.x, g.volumeBar.w) * 130.0); }

void onMotion() {
    touch();
    if (g.seeking) { dragSeek(); return; }
    if (g.draggingVolume) { dragVolume(); return; }
    setHover();
}
void onRelease(uint32_t button) {
    if (button != 0x110) return;
    if (g.seeking) { g.seeking = false; seekTo(g.seekFraction, true); g.pos = g.seekFraction * g.dur; g.dirty = true; }
    g.draggingVolume = false;
}
void onClick(uint32_t button) {
    touch();
    if (button != 0x110) return;
    switch (g.hoverId) {
        case BtnOpen: startOpenDialog(); return;
        case BtnClose: g.running = false; return;
        case BtnPlay: togglePause(); return;
        case BtnPrev: navigate(-1); return;
        case BtnNext: navigate(1); return;
        case BtnMute: toggleMute(); return;
        case BtnSpeed: cycleSpeed(); return;
        case BtnAuto: toggleAutoplay(); return;
        case BtnSubs: cycleTrack("sid", i18n::tr("Subtitles")); return;
        case BtnAudio: cycleTrack("aid", i18n::tr("Audio")); return;
        case BtnFull: toggleFullscreen(); return;
        case Seek: g.seeking = true; dragSeek(); return;
        case Volume: g.draggingVolume = true; dragVolume(); return;
        case Canvas: {
            if (!g.loaded) return;
            const auto now = Clock::now();
            g.clickCount = std::chrono::duration<double>(now - g.lastClick).count() < 0.35 ? g.clickCount + 1 : 1;
            g.lastClick = now;
            if (g.clickCount >= 2) { g.clickCount = 0; toggleFullscreen(); }
            togglePause();  // (a double click pauses and plays again: no change, and goes full screen)
            return;
        }
        default: return;
    }
}
void onWheel(double v) { if (g.loaded) setVolume(g.volume + (v < 0 ? 5 : -5)); }

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
    if (!hidpi::begin(frame, width, height, scale, "end4pC-vid3os")) return nullptr;
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

// Asks mpv for the frame it is showing, as big as the window in real pixels.
cairo_surface_t* videoFrame(const hidpi::Frame& f) {
    if (!g.rctx || !g.loaded || !g.hasVideo) return nullptr;
    if (!g.videoSurf || cairo_image_surface_get_width(g.videoSurf) != f.pw || cairo_image_surface_get_height(g.videoSurf) != f.ph) {
        if (g.videoSurf) cairo_surface_destroy(g.videoSurf);
        g.videoSurf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, f.pw, f.ph);
        cairo_surface_set_device_scale(g.videoSurf, double(f.pw) / f.lw, double(f.ph) / f.lh);
    }
    cairo_surface_flush(g.videoSurf);
    int size[2] = {f.pw, f.ph};
    size_t stride = static_cast<size_t>(cairo_image_surface_get_stride(g.videoSurf));
    const char* format = "bgr0";
    mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, size}, {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>(format)}, {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                                 {MPV_RENDER_PARAM_SW_POINTER, cairo_image_surface_get_data(g.videoSurf)}, {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_render(g.rctx, params) < 0) return nullptr;
    cairo_surface_mark_dirty(g.videoSurf);
    return g.videoSurf;
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
    drawWindow(cr, g.width, g.height, videoFrame(f));
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
void onTopClose(void*, xdg_toplevel*) { g.running = false; }
const xdg_toplevel_listener kToplevelListener = {onTopConfigure, onTopClose, nullptr, nullptr};

void onPEnter(void*, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) { g.enterSerial = serial; g.cursorShape = -1; g.cursorHidden = false; g.inside = true; g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); touch(); setHover(); }
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
    if (!g.display) { std::fprintf(stderr, "end4pC-vid3os: no Wayland display\n"); return false; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    if (wl_display_roundtrip(g.display) < 0 || !g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-vid3os: missing Wayland globals\n"); return false; }
    wl_display_roundtrip(g.display);
    g.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(g.scale, g.surface, g.viewporter, g.fscaleMgr, [] { g.dirty = true; });
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, "Vid3os");
    xdg_toplevel_set_app_id(g.toplevel, "end4pC-vid3os");
    xdg_toplevel_set_min_size(g.toplevel, kMinW, kMinH);
    wl_surface_commit(g.surface);
    return true;
}

void tick() {
    const auto now = Clock::now();
    handleEvents();
    if (g.rctx) {
        const uint64_t flags = mpv_render_context_update(g.rctx);
        if (flags & MPV_RENDER_UPDATE_FRAME) g.dirty = true;
    }
    if (!g.pendingTrack.empty() && now - g.pendingTrackAt > std::chrono::milliseconds(120)) sayTrack();
    {  // what the file dialog answered
        std::vector<std::string> opened;
        { std::lock_guard<std::mutex> lock(g.chosenMutex); opened.swap(g.chosenOpen); }
        for (const std::string& p : opened) openPath(p);
    }
    if (g.repeatKey && now >= g.repeatAt) {
        if (!handleKey(g.repeatKey)) g.repeatKey = 0;
        g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
    }
    if (mtimeOf(colorsPath()) != g_colorsStamp) { loadColors(); g.dirty = true; }
    if (!g.toast.empty()) g.dirty = true;
    // the controls going away: one more picture when the time is up (and the pointer goes with them)
    static bool wasShown = true;
    const bool shown = controlsShown();
    if (shown != wasShown) { wasShown = shown; g.dirty = true; updateCursor(); }
}

// The loop wakes when the next key repeat is due, and when the controls are about to go away.
int pollTimeout() {
    int t = g.loaded && !g.paused ? 80 : !g.toast.empty() ? 100 : 400;
    if (g.repeatKey) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(g.repeatAt - Clock::now()).count();
        t = std::min<int>(t, static_cast<int>(std::max<long long>(1, left)));
    }
    return t;
}

void teardown() {
    if (g.rctx) mpv_render_context_free(g.rctx);
    if (g.mpv) mpv_terminate_destroy(g.mpv);
    if (g.videoSurf) cairo_surface_destroy(g.videoSurf);
    if (g.cursorDev) wp_cursor_shape_device_v1_destroy(g.cursorDev);
    if (g.frameCallback) wl_callback_destroy(g.frameCallback);
    if (g.toplevel) xdg_toplevel_destroy(g.toplevel);
    if (g.xsurface) xdg_surface_destroy(g.xsurface);
    hidpi::detach(g.scale);
    if (g.surface) wl_surface_destroy(g.surface);
    for (auto& b : g.buffers) b.clear();
    if (g.display) { wl_display_flush(g.display); wl_display_disconnect(g.display); }
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
    loadSettings();
    if (!connectWayland()) return 1;

    cairo_surface_t* dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* dcr = mkcr(dummy);
    g_measure = pango_cairo_create_layout(dcr);

    int wakePipe[2];
    if (pipe2(wakePipe, O_CLOEXEC | O_NONBLOCK) < 0) return 1;
    g_wakeFd = wakePipe[1];
    if (!initMpv()) { std::fprintf(stderr, "end4pC-vid3os: mpv could not start\n"); return 1; }
    if (argc > 1) openPath(argv[1]);

    std::signal(SIGTERM, onTermSignal);
    std::signal(SIGINT, onTermSignal);
    std::signal(SIGHUP, onTermSignal);
    std::signal(SIGPIPE, SIG_IGN);
    while (g.running && !g_term) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);
        pollfd fds[] = {{wl_display_get_fd(g.display), POLLIN, 0}, {wakePipe[0], POLLIN, 0}};
        if (poll(fds, 2, pollTimeout()) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & POLLIN) {
            char junk[64];
            while (read(wakePipe[0], junk, sizeof junk) > 0) {}
        }
        tick();
        if (g.dirty) render();
    }
    saveSettings();
    teardown();
    g_object_unref(g_measure);
    cairo_destroy(dcr);
    cairo_surface_destroy(dummy);
    return 0;
}
