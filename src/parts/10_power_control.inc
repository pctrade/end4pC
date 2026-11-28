

struct PowerAction { const char* label; const char* icon; };
constexpr PowerAction kPowerActions[] = {
    {"Lock", "lock"}, {"Logout", "logout-2"}, {"Suspend", "pause-circle"},
    {"Hibernate", "moon-sleep"}, {"Reboot", "restart"}, {"Shutdown", "power"},
};
constexpr int kPowerCount = 6;

struct PowerLayout { double rowX, rowY, rowW, rowH, slotW; };
PowerLayout powerLayout(const PowerMenu& P) {
    PowerLayout l{};
    l.rowW = std::min(P.w * 0.78, 1180.0);
    l.rowH = 230;
    l.slotW = l.rowW / kPowerCount;
    l.rowX = (P.w - l.rowW) / 2;
    l.rowY = (P.h - l.rowH) / 2;
    return l;
}
Rect powerSlot(const PowerLayout& l, int i, bool lit) {
    const double grow = lit ? 36 : 0;
    return {l.rowX + i * l.slotW, l.rowY - grow, l.slotW, l.rowH + 2 * grow};
}
int powerAt(const PowerMenu& P, double x, double y) {
    const PowerLayout l = powerLayout(P);
    if (!Rect{l.rowX, l.rowY, l.rowW, l.rowH}.contains(x, y)) return -1;
    return std::clamp(static_cast<int>((x - l.rowX) / l.slotW), 0, kPowerCount - 1);
}
int powerLit(const PowerMenu& P) { return P.hover >= 0 ? P.hover : P.selected; }

void drawPowerMenu(App& app) {
    PowerMenu& P = app.power;
    if (!P.open() || !P.configured || P.w <= 0 || P.h <= 0) return;
    const int W = P.w, H = P.h;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, P.scale.factor(), "end4pC-power")) return;
    cairo_surface_t* surface = hf.surface;
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // A scrim over everything (the compositor blurs what is behind it, see the layer rule), then the row.
    const double rev = std::clamp(P.anim.reveal, 0.0, 1.0);
    setSource(cr, app.color("scrim"), 0.25 * rev);
    cairo_paint(cr);
    popGroupBegin(cr);
    const PowerLayout l = powerLayout(P);
    const int lit = powerLit(P);
    roundedRect(cr, l.rowX, l.rowY, l.rowW, l.rowH, 40);
    setSource(cr, app.color("surface_container"), 0.78);
    cairo_fill(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    for (int i = 0; i < kPowerCount; i++) {
        const bool on = i == lit, danger = i == kPowerCount - 1;
        const Rect r = powerSlot(l, i, on);
        if (on) {
            roundedRect(cr, r.x, r.y, r.w, r.h, 40);
            setSource(cr, app.color(danger ? "error_container" : "primary_container"));
            cairo_fill(cr);
        }
        const Color ink = app.color(danger ? (on ? "on_error_container" : "error") : (on ? "on_primary_container" : "on_surface"));
        const int isz = on ? 84 : 52;
        const double cy = l.rowY + l.rowH * 0.44;
        const bool armed = P.armed == i;
        drawIcon(cr, armed ? "check-circle" : kPowerActions[i].icon, r.x + (r.w - isz) / 2, cy - isz / 2.0, isz, ink, on);
        PangoFontDescription* fd2 = pango_font_description_from_string(on ? "Sans 13" : "Sans 12");
        pango_layout_set_font_description(layout, fd2);
        pango_font_description_free(fd2);
        pango_layout_set_text(layout, armed ? "Confirm" : kPowerActions[i].label, -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, ink);
        cairo_move_to(cr, r.x + (r.w - tw) / 2.0, l.rowY + l.rowH - 44 + (on ? 14 : 0));
        pango_cairo_show_layout(cr, layout);
    }
    g_object_unref(layout);
    if (rev > 0.001) popFadeEnd(cr, rev, W / 2.0, H / 2.0, 0.97);
    else cairo_pattern_destroy(cairo_pop_group(cr));
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, P.surface, P.blur, W, H, [&](cairo_t* m) {  // the whole screen: the scrim is what is blurred
        if (rev > 0.001) cairo_paint(m);
    });
    hidpi::present(P.surface, buffer, hf, P.scale);
}

void closePowerMenu(App& app) {
    PowerMenu& P = app.power;
    if (!P.open()) return;
    if (P.layer) zwlr_layer_surface_v1_destroy(P.layer);
    releaseBlurSurface(P.blur);
    hidpi::detach(P.scale);
    wl_surface_destroy(P.surface);
    P = PowerMenu{};
    app.onPower = false;
    updateCursor(app);
    malloc_trim(0);
}

void startLock();

void runSession(const char* then) {
    const std::string cmd = wmKillWindowsCommand() + then;
    runDetached({"bash", "-c", cmd.c_str()});
}
void powerRunAction(int i) {
    switch (i) {
        case 0: startLock(); break;
        case 1: runSession(wmQuitCommand().c_str()); break;
        case 2: runDetached({"bash", "-c", "systemctl suspend || loginctl suspend"}); break;
        case 3: runDetached({"bash", "-c", "systemctl hibernate || loginctl hibernate"}); break;
        case 4: runSession("systemctl reboot || loginctl reboot"); break;
        case 5: runSession("systemctl poweroff || loginctl poweroff"); break;
    }
}
bool islandOpenPower(App& app);
bool islandTogglePower(App& app);
void powerActivate(App& app, int i) {
    PowerMenu& P = app.power;
    if (i != 0 && P.armed != i) {
        P.armed = i;
        P.selected = i;
        drawPowerMenu(app);
        return;
    }
    closePowerMenu(app);
    powerRunAction(i);
}

void onPowerConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->power.w = static_cast<int>(w);
    app->power.h = static_cast<int>(h);
    if (!app->power.configured) app->power.anim.animStart = std::chrono::steady_clock::now();
    app->power.configured = true;
    drawPowerMenu(*app);
}
void onPowerClosed(void* d, zwlr_layer_surface_v1*) { closePowerMenu(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kPowerListener = {onPowerConfigure, onPowerClosed};

void closeSelector(App& app);
void closeNotifPanel(App& app);
void openPowerMenu(App& app) {
    if (islandOpenPower(app)) return;
    if (app.power.open()) return;
    closeSelector(app);
    closeLauncher(app);
    closeNotifPanel(app);
    closeMediaPanel(app); closeClockPanel(app);
    PowerMenu& P = app.power;
    P = PowerMenu{};
    popStartQuick(P.anim, 0.16, 0.11);
    P.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, P.scale, P.surface, [&app] { drawPowerMenu(app); });
    P.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, P.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-power");
    zwlr_layer_surface_v1_set_anchor(P.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(P.layer, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(P.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(P.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(P.layer, &kPowerListener, &app);
    wl_surface_commit(P.surface);
}
void retractPowerMenu(App& app) {
    PowerMenu& P = app.power;
    if (!P.open() || popIsOut(P.anim)) return;
    if (!P.configured) { closePowerMenu(app); return; }
    popRetract(P.anim, std::chrono::steady_clock::now());
    P.hover = -1;
    drawPowerMenu(app);
}
void togglePowerMenu(App& app) {
    if (islandTogglePower(app)) return;
    PowerMenu& P = app.power;
    if (!P.open()) { openPowerMenu(app); return; }
    if (popIsOut(P.anim)) {
        popReverse(P.anim, std::chrono::steady_clock::now());
        drawPowerMenu(app);
    } else retractPowerMenu(app);
}
void tickPowerMenu(App& app, std::chrono::steady_clock::time_point now) {
    PowerMenu& P = app.power;
    if (!P.open() || !P.configured) return;
    bool moving = false;
    if (popTick(P.anim, now, moving)) { closePowerMenu(app); return; }
    if (moving) drawPowerMenu(app);
}
std::chrono::steady_clock::time_point powerWake(const App& app) {
    if (!app.power.open() || !app.power.configured) return std::chrono::steady_clock::time_point::max();
    return popWake(app.power.anim.phase, app.power.anim.settleAt, std::chrono::steady_clock::now());
}

void powerMotion(App& app) {
    PowerMenu& P = app.power;
    if (popIsOut(P.anim)) return;
    const int h = powerAt(P, app.pointerX, app.pointerY);
    int keep = h;
    if (h < 0 && P.hover >= 0 && powerSlot(powerLayout(P), P.hover, true).contains(app.pointerX, app.pointerY)) keep = P.hover;
    if (keep == P.hover) return;
    P.hover = keep;
    if (P.armed != keep) P.armed = -1;
    drawPowerMenu(app);
    updateCursor(app);
}
void powerClick(App& app) {
    PowerMenu& P = app.power;
    if (popIsOut(P.anim)) return;
    const int i = P.hover >= 0 ? P.hover : powerAt(P, app.pointerX, app.pointerY);
    if (i >= 0) powerActivate(app, i);
    else retractPowerMenu(app);
}
bool powerKey(App& app, uint32_t key) {
    PowerMenu& P = app.power;
    if (!app.xkbState || popIsOut(P.anim)) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, key + 8);
    switch (sym) {
        case XKB_KEY_Left: case XKB_KEY_Up: P.selected = P.selected < 0 ? kPowerCount - 1 : (P.selected + kPowerCount - 1) % kPowerCount; P.hover = -1; P.armed = -1; break;
        case XKB_KEY_Right: case XKB_KEY_Down: case XKB_KEY_Tab: P.selected = P.selected < 0 ? 0 : (P.selected + 1) % kPowerCount; P.hover = -1; P.armed = -1; break;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter: if (powerLit(P) >= 0) powerActivate(app, powerLit(P)); return false;
        default: return false;
    }
    drawPowerMenu(app);
    return false;
}

void restartShell() { runDetached({"bash", (assetsDir() + "/../scripts/restart.sh").c_str()}); }
std::string mediaCoverPath(const App& app);
cairo_surface_t* loadSquare(const std::string& path, int size);
enum CcId { CcNone = 0, CcPower, CcSettings, CcWifi, CcBt, CcPrev, CcPlay, CcNext, CcAwake, CcNight, CcRec, CcShot, CcBright, CcVol, CcMute, CcUpdates, CcWall, CcMedia, CcRestart };
struct CcItem { Rect r; int id; };
constexpr double kCcW = 392, kCcPad = 16, kCcGap = 12, kCcRoom = 20;
constexpr double kCcHeaderH = 44, kCcRowH = 188, kCcWideH = 64;
constexpr double kCcH = kCcPad * 2 + kCcHeaderH + kCcRowH * 2 + kCcWideH + kCcGap * 3;
constexpr int kCcSurfW = static_cast<int>(kCcW + 2 * kCcRoom), kCcSurfH = static_cast<int>(kCcH + 2 * kCcRoom);

std::vector<CcItem> ccItems() {
    std::vector<CcItem> v;
    const double inner = kCcW - 2 * kCcPad, leftW = 196, rightW = inner - leftW - kCcGap;
    double y = kCcPad;
    v.push_back({{kCcW - kCcPad - 40, y + 2, 40, 40}, CcSettings});
    v.push_back({{kCcW - kCcPad - 88, y + 2, 40, 40}, CcPower});
    v.push_back({{kCcW - kCcPad - 136, y + 2, 40, 40}, CcRestart});
    y += kCcHeaderH + kCcGap;
    v.push_back({{kCcPad, y, leftW, 88}, CcWifi});
    v.push_back({{kCcPad, y + 100, leftW, 88}, CcBt});
    const double mx = kCcPad + leftW + kCcGap;
    v.push_back({{mx, y, rightW, kCcRowH}, CcMedia});
    v.push_back({{mx + 10, y + kCcRowH - 56, 40, 44}, CcPrev});
    v.push_back({{mx + rightW / 2 - 24, y + kCcRowH - 58, 48, 48}, CcPlay});
    v.push_back({{mx + rightW - 50, y + kCcRowH - 56, 40, 44}, CcNext});
    y += kCcRowH + kCcGap;
    const int toggles[4] = {CcAwake, CcNight, CcRec, CcShot};
    for (int i = 0; i < 4; i++)
        v.push_back({{kCcPad + 16 + (i % 2) * (64 + 36), y + 16 + (i / 2) * (64 + 28), 64, 64}, toggles[i]});
    const double sw = (rightW - kCcGap) / 2;
    v.push_back({{mx, y, sw, kCcRowH}, CcBright});
    v.push_back({{mx + sw + kCcGap, y, sw, kCcRowH}, CcVol});
    v.push_back({{mx + sw + kCcGap + (sw - 40) / 2, y + kCcRowH - 48, 40, 40}, CcMute});
    y += kCcRowH + kCcGap;
    const double ww = (inner - kCcGap) / 2;
    v.push_back({{kCcPad, y, ww, kCcWideH}, CcUpdates});
    v.push_back({{kCcPad + ww + kCcGap, y, ww, kCcWideH}, CcWall});
    return v;
}
int ccAt(double x, double y) {
    const auto items = ccItems();
    int hit = CcNone;
    for (const auto& it : items)
        if (it.r.contains(x, y)) hit = it.id;
    return hit;
}

std::string ccFileRead(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::string ccCacheDir() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return (x && *x ? std::string(x) : homeDir() + "/.cache") + "/end4pC";
}
void runUpdatesTerminal(App& app);

void ccRunScript(const char* a, const char* b = nullptr) {
    const std::string script = assetsDir() + "/../scripts/cc.sh";
    if (b) runDetached({"bash", script.c_str(), a, b});
    else runDetached({"bash", script.c_str(), a});
}
void ccReadStatus(App& app) {
    ControlCenter& C = app.cc;
    const std::string path = ccCacheDir() + "/cc.txt";
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || st.st_mtime == C.stamp) return;
    C.stamp = st.st_mtime;
    std::istringstream in(ccFileRead(path));
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "wifi") C.wifi = v; else if (k == "wifiName") C.wifiName = v;
        else if (k == "bt") C.bt = v; else if (k == "btName") C.btName = v; else if (k == "night") C.night = v;
    }
}
void setKeepAwake(App& app, bool on) {
    if (on && !app.idler && app.idleMgr && mainBar(app) && mainBar(app)->surface)
        app.idler = zwp_idle_inhibit_manager_v1_create_inhibitor(app.idleMgr, mainBar(app)->surface);
    else if (!on && app.idler) {
        zwp_idle_inhibitor_v1_destroy(app.idler);
        app.idler = nullptr;
    }
    wl_display_flush(app.display);
}

std::string ccSignature(const App& app) {
    const ControlCenter& C = app.cc;
    return app.netKind + C.wifi + C.wifiName + C.bt + C.btName + C.night + app.musicStatus + app.musicTitle + app.musicArtist + std::to_string(app.volume) + (app.muted ? "m" : "u") +
           std::to_string(app.brightness) + std::string(app.idler ? "k" : "s") + (app.recording ? "r" : "i") + std::to_string(app.updatesTotal) + (app.updatesChecking ? "u" : "d") + std::to_string(C.hover) + std::to_string(C.drag);
}

void ccDrawRadioPage(App& app, cairo_t* cr, PangoLayout* layout, double x0);
void ccOpenRadio(App& app, int page);
void ccRadioMotion(App& app);
void ccRadioClick(App& app);
void ccRadioBubble(App& app, cairo_t* cr);

void drawControlCenter(App& app) {
    ControlCenter& C = app.cc;
    if (!C.open() || !C.configured) return;
    C.signature = ccSignature(app);
    if (!app.avatarTried) loadAvatar(app);
    const int W = kCcSurfW, H = kCcSurfH;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, C.scale.factor(), "end4pC-control")) return;
    cairo_surface_t* surface = hf.surface;
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const double rev = std::max(0.0, C.anim.reveal);
    double gox = 0, goy = 0;
    popGrowOrigin(app, kCcRoom, kCcRoom, kCcW, kCcH, C.along, gox, goy);
    popGroupBegin(cr);

    static cairo_surface_t* shadow = makeShadowMask(kCcW, kCcH, 32);
    setSource(cr, app.color("shadow"), 0.3);
    cairo_mask_surface(cr, shadow, kCcRoom - kLaunShadow, kCcRoom - kLaunShadow + 2);
    cairo_translate(cr, kCcRoom, kCcRoom);
    cairo_save(cr);
    roundedRect(cr, 0, 0, kCcW, kCcH, 32);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill(cr);
    cairo_restore(cr);
    roundedRect(cr, 0, 0, kCcW, kCcH, 32);
    setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    cairo_fill(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    const double view = easeInOutCubic(C.slide);
    cairo_save(cr);
    if (view >= 1) cairo_rectangle(cr, 0, 0, 0, 0);
    else roundedRect(cr, 0, 0, kCcW, kCcH, 32);
    cairo_clip(cr);
    cairo_translate(cr, -view * kCcW, 0);
    const auto now = std::chrono::steady_clock::now();
    const auto tiles = ccItems();
    if (C.hover != C.tickId) { C.tickId = C.hover; C.tickSince = now; }
    C.ticking = false;
    auto label = [&](const std::string& t, const char* font, double x, double y, const Color& c, double a = 1, double maxW = 0, bool center = false) {
        PangoFontDescription* fd2 = pango_font_description_from_string(font);
        pango_layout_set_font_description(layout, fd2);
        pango_font_description_free(fd2);
        pango_layout_set_ellipsize(layout, maxW > 0 ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
        pango_layout_set_width(layout, maxW > 0 ? static_cast<int>(maxW * PANGO_SCALE) : -1);
        pango_layout_set_text(layout, t.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        if (maxW > 0 && pango_layout_is_ellipsized(layout) && C.hover != 0 && C.drag == 0) {
            bool inside = false;
            for (const auto& it : tiles) if (it.id == C.hover && it.r.contains(x + 2, y + 2)) inside = true;
            if (inside) {
                pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
                pango_layout_set_width(layout, -1);
                int fw = 0, fh = 0;
                pango_layout_get_pixel_size(layout, &fw, &fh);
                const double gap = 36, secs = std::chrono::duration<double>(now - C.tickSince).count() - 0.6;
                const double off = secs > 0 ? std::fmod(secs * 42.0, fw + gap) : 0;
                cairo_save(cr);
                cairo_rectangle(cr, x, y, maxW, th + 2);
                cairo_clip(cr);
                setSource(cr, c, a);
                cairo_move_to(cr, x - off, y);
                pango_cairo_show_layout(cr, layout);
                cairo_move_to(cr, x - off + fw + gap, y);
                pango_cairo_show_layout(cr, layout);
                cairo_restore(cr);
                C.ticking = true;
                return th;
            }
        }
        setSource(cr, c, a);
        cairo_move_to(cr, center ? x + (maxW - tw) / 2.0 : x, y);
        pango_cairo_show_layout(cr, layout);
        return th;
    };
    const Color onSurf = app.color("on_surface"), variant = app.color("on_surface_variant"), primary = app.color("primary"), onPrimary = app.color("on_primary");
    auto circle = [&](double cx, double cy, double r, const Color& c, double a = 1) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        setSource(cr, c, a);
        cairo_fill(cr);
    };
    const auto items = ccItems();
    auto rectOf = [&](int id) { for (const auto& it : items) if (it.id == id) return it.r; return Rect{}; };
    const bool anyDown = C.drag != 0;
    auto hot = [&](int id) { return C.hover == id && !anyDown; };

    {
        const std::string name = shownName(app);
        const Rect r0 = rectOf(CcSettings);
        const double hy = kCcPad;
        if (app.avatar) {
            cairo_save(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, kCcPad + 22, hy + 22, 22, 0, 2 * M_PI);
            cairo_clip(cr);
            const double sc = 44.0 / cairo_image_surface_get_width(app.avatar);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, app.avatar, kCcPad / sc, hy / sc);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            circle(kCcPad + 22, hy + 22, 22, app.color("primary_container"));
            drawIcon(cr, "user-circle", kCcPad + 22 - 13, hy + 22 - 13, 26, app.color("on_primary_container"));  // (no picture chosen: a person, like the bar module and the lock screen)
        }
        const int th0 = label(name, "Sans Bold 13", kCcPad + 56, hy + 3, onSurf, 1, std::max(60.0, rectOf(CcRestart).x - (kCcPad + 56) - 10));  // (a long name ends in "…" before the buttons)
        label("Control center", "Sans 10", kCcPad + 56, hy + 3 + th0 + 1, variant);
        for (int id : {CcRestart, CcPower, CcSettings}) {
            const Rect r = rectOf(id);
            circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, hot(id) ? app.color("surface_container_highest") : app.color("surface_container_high"));
            drawIcon(cr, id == CcPower ? "power" : id == CcRestart ? "restart" : "settings", r.x + 9, r.y + 9, 22, onSurf, hot(id));
        }
        (void)r0;
    }

    auto wide = [&](int id, const char* icon, const std::string& title, const std::string& sub, bool active, bool absent) {
        const Rect r = rectOf(id);
        roundedRect(cr, r.x, r.y, r.w, r.h, 30);
        setSource(cr, app.color(hot(id) && !absent ? "surface_container_highest" : "surface_container_high"));
        cairo_fill(cr);
        const double a = absent ? 0.4 : 1.0, d = r.h >= 80 ? 56 : 44;
        circle(r.x + 16 + d / 2, r.y + r.h / 2, d / 2, active ? primary : app.color("surface_container_highest"), absent ? 0.5 : 1);
        drawIcon(cr, icon, r.x + 16 + (d - 26) / 2, r.y + (r.h - 26) / 2, 26, active ? onPrimary : onSurf, active);
        const double tx = r.x + 16 + d + 12, tw = r.x + r.w - 12 - tx;
        const int th = label(title, "Sans Bold 11", tx, r.y + r.h / 2 - 20, onSurf, a, tw);
        label(sub, "Sans 9", tx, r.y + r.h / 2 - 20 + th + 1, variant, a, tw);
    };
    if (C.wifi == "absent") {
        const bool up = app.netKind != "none";
        wide(CcWifi, "settings_ethernet", app.netKind == "vpn" ? "VPN" : "Ethernet", up ? "Connected" : "No connection", up, false);
    } else {
        wide(CcWifi, "wi-fi-router", "Wi-Fi", C.wifi == "off" ? "Off" : (C.wifiName.empty() ? "On" : C.wifiName), C.wifi == "on", false);
    }
    wide(CcBt, "bluetooth", "Bluetooth", C.bt == "absent" ? "Not available" : C.bt == "off" ? "Off" : (C.btName.empty() ? "On" : C.btName), C.bt == "on", C.bt == "absent");

    {
        const Rect first = rectOf(CcPrev), last = rectOf(CcNext), play = rectOf(CcPlay);
        const double mx = first.x - 10, my = first.y + 56 - kCcRowH, mw = last.x + last.w + 10 - mx;
        roundedRect(cr, mx, my, mw, kCcRowH, 30);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
        const bool playing = app.musicStatus == "Playing", have = !app.musicStatus.empty();
        const double art = 48;
        if (C.coverKey != app.musicArtUrl || (!C.cover && app.musicArt)) {
            if (C.cover) cairo_surface_destroy(C.cover);
            C.cover = app.musicArt ? loadSquare(mediaCoverPath(app), 96) : nullptr;
            C.coverKey = app.musicArtUrl;
        }
        cairo_surface_t* cover = C.cover ? C.cover : app.musicArt;
        const double coverPx = C.cover ? 96 : kMusicArt;
        cairo_save(cr);
        roundedRect(cr, mx + 14, my + 14, art, art, 14);
        if (cover) {
            cairo_clip(cr);
            const double sc = art / coverPx;
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, cover, (mx + 14) / sc, (my + 14) / sc);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint(cr);
        } else {
            setSource(cr, app.color("surface_container_highest"));
            cairo_fill(cr);
        }
        cairo_restore(cr);
        if (!cover) drawIcon(cr, "music-note", mx + 14 + 12, my + 14 + 12, 24, variant);
        const double ty = my + 14 + art + 10;
        const int th = label(have ? app.musicTitle : "Nothing playing", "Sans Bold 11", mx + 14, ty, onSurf, have ? 1 : 0.6, mw - 28);
        if (have) label(app.musicArtist, "Sans 9", mx + 14, ty + th + 1, variant, 1, mw - 28);
        for (int id : {CcPrev, CcNext}) {
            const Rect r = rectOf(id);
            if (hot(id)) { roundedRect(cr, r.x, r.y, r.w, r.h, 14); setSource(cr, app.color("surface_container_highest")); cairo_fill(cr); }
            drawIcon(cr, id == CcPrev ? "skip-previous" : "skip-next", r.x + (r.w - 24) / 2, r.y + (r.h - 24) / 2, 24, onSurf);
        }
        circle(play.x + play.w / 2, play.y + play.h / 2, play.w / 2 - (hot(CcPlay) ? 0 : 2), primary);
        drawIcon(cr, playing ? "pause" : "play", play.x + 12, play.y + 12, 24, onPrimary, true);
    }

    auto toggle = [&](int id, const char* icon, bool active) {
        const Rect r = rectOf(id);
        circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 - (hot(id) ? 0 : 2), active ? primary : app.color(hot(id) ? "secondary_container" : "surface_container_highest"));
        drawIcon(cr, icon, r.x + (r.w - 28) / 2, r.y + (r.h - 28) / 2, 28, active ? onPrimary : onSurf, active);
    };
    {
        const Rect a = rectOf(CcAwake), b = rectOf(CcShot);
        roundedRect(cr, kCcPad, a.y - 16, 196, b.y + b.h + 16 - (a.y - 16), 30);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
    }
    toggle(CcAwake, "cup-hot", app.cfg.keepAwake != 0);
    const bool nightAuto = C.night.compare(0, 4, "auto") == 0;
    toggle(CcNight, "moon", C.night == "on" || C.night == "autoon");
    if (nightAuto) {
        const Rect r = rectOf(CcNight);
        const bool act = C.night == "autoon";
        PangoFontDescription* f = pango_font_description_from_string("Sans Bold 7");
        pango_layout_set_font_description(layout, f);
        pango_layout_set_text(layout, "AUTO", -1);
        int aw = 0, ah = 0;
        pango_layout_get_pixel_size(layout, &aw, &ah);
        cairo_move_to(cr, r.x + (r.w - aw) / 2.0, r.y + r.h - ah - 9);
        setSource(cr, act ? onPrimary : onSurf, 0.85);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(f);
    }
    toggle(CcRec, app.recording ? "stop-circle" : "videocamera-record", app.recording);
    toggle(CcShot, "camera", false);
    if (C.night == "absent") { const Rect r = rectOf(CcNight); circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, app.color("surface_container_high"), 0.5); }

    const Color tertiary = app.color("tertiary"), onTertiary = app.color("on_tertiary");
    auto slider = [&](int id, double value, const char* icon, bool muted, bool loud = false, double mark = -1) {
        const Rect r = rectOf(id);
        roundedRect(cr, r.x, r.y, r.w, r.h, 30);
        setSource(cr, app.color(hot(id) || C.drag == id ? "surface_container_highest" : "surface_container_high"));
        cairo_fill(cr);
        cairo_save(cr);
        roundedRect(cr, r.x, r.y, r.w, r.h, 30);
        cairo_clip(cr);
        const double fh = std::max(0.0, std::min(1.0, value)) * r.h;
        setSource(cr, loud ? tertiary : primary, muted ? 0.35 : 1);
        cairo_rectangle(cr, r.x, r.y + r.h - fh, r.w, fh);
        cairo_fill(cr);
        if (mark > 0) {  // where 100 % is, when the slider goes higher
            const double my = r.y + r.h - r.h * mark;
            setSource(cr, my >= r.y + r.h - fh && !muted ? (loud ? onTertiary : onPrimary) : onSurf, 0.55);
            cairo_rectangle(cr, r.x + 14, my - 1, r.w - 28, 2);
            cairo_fill(cr);
        }
        cairo_restore(cr);
        const bool onFill = fh > 27;
        drawIcon(cr, icon, r.x + (r.w - 26) / 2, r.y + r.h - 40, 26, onFill && !muted ? (loud ? onTertiary : onPrimary) : onSurf, false);
    };
    slider(CcBright, app.brightness < 0 ? 0 : app.brightness / 100.0, "sun", false);
    slider(CcVol, app.muted ? 0.0 : volFrac(app, app.volume), app.muted || app.volume == 0 ? "volume-cross" : app.volume < 50 ? "volume-small" : "volume-loud", app.muted,
           volLoud(app.volume), volMark(app));

    {
        const int total = app.updatesTotal;
        wide(CcUpdates, "download-square", "Updates", total < 0 ? "Not checked" : total == 0 ? "Up to date" : std::to_string(total) + " pending", total > 0, false);
        wide(CcWall, "gallery", "Wallpaper", "Pick one", false, false);
    }
    cairo_restore(cr);
    if (C.page != CcPageNone && C.slide > 0) {
        cairo_save(cr);
        roundedRect(cr, 0, 0, kCcW, kCcH, 32);
        cairo_clip(cr);
        ccDrawRadioPage(app, cr, layout, (1 - view) * kCcW);
        cairo_restore(cr);
    }
    ccRadioBubble(app, cr);

    g_object_unref(layout);
    if (rev > 0.001) popGroupEnd(cr, rev, gox, goy, C.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8));
    else cairo_pattern_destroy(cairo_pop_group(cr));
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, C.surface, C.blur, W, H, [&](cairo_t* m) {
        if (rev <= 0.001) return;
        popPaintGrown(m, rev, gox, goy, 1.0, [&] {
            cairo_translate(m, kCcRoom, kCcRoom);
            roundedRect(m, 0, 0, kCcW, kCcH, 32);
            cairo_fill(m);
        });
    });
    hidpi::present(C.surface, buffer, hf, C.scale);
}

void closeControlCenter(App& app) {
    ControlCenter& C = app.cc;
    if (!C.open()) return;
    if (C.layer) zwlr_layer_surface_v1_destroy(C.layer);
    releaseBlurSurface(C.blur);
    hidpi::detach(C.scale);
    wl_surface_destroy(C.surface);
    if (C.cover) cairo_surface_destroy(C.cover);
    C = ControlCenter{};
    app.onCc = false;
    updateCursor(app);
    malloc_trim(0);
}

void onCcConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->cc.configured) app->cc.anim.animStart = std::chrono::steady_clock::now();
    app->cc.configured = true;
    drawControlCenter(*app);
}
void onCcClosed(void* d, zwlr_layer_surface_v1*) { closeControlCenter(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kCcListener = {onCcConfigure, onCcClosed};

void startBrightnessJob(App& app, const char* mode, int percent, const std::string& out);
void brightnessFollowFocus(App& app);
std::string wmFocusedOutputInfo();
void pollMedia(App& app);
void changeBrightness(App& app, int delta, int absolute = -1, std::string output = "");
void showOsd(App& app, int kind);
void toggleSettings();
void toggleTools();
void toggleTasks();
void togglePlay3r(const std::string& argument);
void openFil3s(const std::string& argument);
void openT3xt(const std::string& argument);
void openImag3s(const std::string& argument);
void openVid3os(const std::string& argument);
pid_t mediaSend(App& app, std::initializer_list<const char*> args);
void setDoNotDisturb(App& app, bool on);
void runSystemUpdate(const App& app) {
    const std::string script = assetsDir() + "/../scripts/system-update.sh";
    runDetached({app.cfg.terminal.c_str(), "-e", "bash", script.c_str(), app.cfg.updatesAur ? "1" : "0", app.cfg.updatesFlatpak ? "1" : "0"});
}

// forceSide: 1 opens it at the left of the screen, 2 at the right (a hot corner: on the side of the corner); 0 as usual (the right, or under the avatar).
void openControlCenter(App& app, bool fromAvatar = false, int forceSide = 0) {
    if (app.cc.open()) return;
    closeSelector(app);
    closeLauncher(app);
    closeNotifPanel(app);
    closeMediaPanel(app); closeClockPanel(app);
    closePowerMenu(app);
    ControlCenter& C = app.cc;
    C = ControlCenter{};
    popStart(C.anim);
    C.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, C.scale, C.surface, [&app] { drawControlCenter(app); });
    C.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, C.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-control");
    const bool vert = sideBar(app), atBottom = app.cfg.barPosition == "bottom";
    constexpr uint32_t kB = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, kT = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, kL = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, kR = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const auto& ll = app.cfg.layoutLeft;
    const bool leftSide = !vert && (forceSide == 1 || (forceSide == 0 && fromAvatar && std::find(ll.begin(), ll.end(), "avatar") != ll.end()));
    const bool underAvatar = !vert && fromAvatar && app.hoverBar && hasSlot(*app.hoverBar, "avatar");  // (opens under the avatar wherever it sits in the bar)
    const bool avatarLow = vert && sideModuleAtBottom(app, "avatar", false);
    zwlr_layer_surface_v1_set_anchor(C.layer, vert ? ((avatarLow ? kB : kT) | (app.cfg.barPosition == "right" ? kR : kL)) : ((atBottom ? kB : kT) | (leftSide || underAvatar ? kL : kR)));
    if (vert) {
        const int side = std::max(0, sideCardEdge(app) - static_cast<int>(kCcRoom)), top = app.cfg.frame + 12 - static_cast<int>(kCcRoom);
        zwlr_layer_surface_v1_set_margin(C.layer, avatarLow ? 0 : top, app.cfg.barPosition == "right" ? side : 0, avatarLow ? top : 0, app.cfg.barPosition == "left" ? side : 0);
        if (fromAvatar && app.hoverBar) {
            const double sideMargin = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
            const double screenLen = app.hoverBar->width + 2.0 * sideMargin;
            C.along = app.pointerY + sideMargin - (avatarLow ? screenLen - kCcSurfH - top : top);
        }
    } else {
        int edge = app.cfg.frame + kBarHeight + 12;
        if (app.cfg.barStyle == "float" || app.cfg.barStyle == "island") edge += kBarGap;
        const int margin = std::max(0, edge - static_cast<int>(kCcRoom));
        const int sideM = std::max(0, app.cfg.frame + 12 - static_cast<int>(kCcRoom));
        if (underAvatar) {
            const double sideMargin = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
            const double screenLen = app.hoverBar->width + 2.0 * sideMargin;
            const double center = slotX(*app.hoverBar, "avatar") + moduleWidth(app, *app.hoverBar, "avatar") / 2.0 + app.cfg.frame;
            const double surfLeft = std::clamp(center + sideMargin - kCcSurfW / 2.0, static_cast<double>(sideM), std::max<double>(sideM, screenLen - kCcSurfW - sideM));
            zwlr_layer_surface_v1_set_margin(C.layer, atBottom ? 0 : margin, 0, atBottom ? margin : 0, static_cast<int>(surfLeft));
            C.along = center + sideMargin - surfLeft;
        } else {
            zwlr_layer_surface_v1_set_margin(C.layer, atBottom ? 0 : margin, leftSide ? 0 : sideM, atBottom ? margin : 0, leftSide ? sideM : 0);
            if (fromAvatar && app.hoverBar) {
                const double sideMargin = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
                const double screenLen = app.hoverBar->width + 2.0 * sideMargin;
                C.along = app.pointerX + sideMargin - (leftSide ? sideM : screenLen - kCcSurfW - sideM);
            }
        }
    }
    zwlr_layer_surface_v1_set_size(C.layer, kCcSurfW, kCcSurfH);
    zwlr_layer_surface_v1_set_exclusive_zone(C.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(C.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(C.layer, &kCcListener, &app);
    wl_surface_commit(C.surface);
    ccRunScript("status");
    C.nextPoll = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    pollMedia(app);
    brightnessFollowFocus(app);
    ccReadStatus(app);
}
void retractControlCenter(App& app) {
    ControlCenter& C = app.cc;
    if (!C.open() || popIsOut(C.anim)) return;
    if (!C.configured) { closeControlCenter(app); return; }
    popRetract(C.anim, std::chrono::steady_clock::now());
    C.hover = 0;
    C.drag = 0;
    drawControlCenter(app);
}

void toggleControlCenter(App& app, bool fromAvatar = false, int forceSide = 0) {
    ControlCenter& C = app.cc;
    if (!C.open()) { openControlCenter(app, fromAvatar, forceSide); return; }
    if (popIsOut(C.anim)) {
        popReverse(C.anim, std::chrono::steady_clock::now());
        drawControlCenter(app);
    } else retractControlCenter(app);
}

void tickControlCenter(App& app, std::chrono::steady_clock::time_point now) {
    ControlCenter& C = app.cc;
    if (!C.open() || !C.configured) return;
    bool moving = false;
    if (popTick(C.anim, now, moving)) { closeControlCenter(app); return; }
    if (moving) drawControlCenter(app);
}
std::chrono::steady_clock::time_point controlWake(const App& app) {
    if (!app.cc.open() || !app.cc.configured) return std::chrono::steady_clock::time_point::max();
    return popWake(app.cc.anim.phase, app.cc.anim.settleAt, std::chrono::steady_clock::now());
}

Rect ccCard() { return {kCcRoom, kCcRoom, kCcW, kCcH}; }
void ccMotion(App& app) {
    ControlCenter& C = app.cc;
    if (popIsOut(C.anim)) return;
    const double x = app.pointerX - kCcRoom, y = app.pointerY - kCcRoom;
    if (C.drag != 0) {
        const Rect r = [&] { for (const auto& it : ccItems()) if (it.id == C.drag) return it.r; return Rect{}; }();
        const double f = std::clamp(1.0 - (y - r.y) / r.h, 0.0, 1.0);
        const auto now = std::chrono::steady_clock::now();
        if (C.drag == CcBright) {
            changeBrightness(app, 0, static_cast<int>(std::lround(f * 100)));
        } else {
            const int v = std::min(volFromFrac(app, f), app.cfg.volumeMax);
            if (v != app.volume || app.muted) {
                app.volume = v;
                if (now - C.lastSend > std::chrono::milliseconds(90)) {
                    C.lastSend = now;
                    const std::string pct = std::to_string(v) + "%";
                    runDetached({"wpctl", "set-volume", "-l", volLimit(app).c_str(), "@DEFAULT_AUDIO_SINK@", pct.c_str()});
                }
            }
        }
        drawControlCenter(app);
        return;
    }
    if (C.page != CcPageNone) { ccRadioMotion(app); return; }
    const int h = ccAt(x, y);
    if (h == C.hover) return;
    C.hover = h;
    drawControlCenter(app);
    updateCursor(app);
}
// The wheel over the brightness or the volume slider of the control center: one step per notch (down for dir > 0).
bool ccWheel(App& app, int dir) {
    ControlCenter& C = app.cc;
    if (popIsOut(C.anim) || C.page != CcPageNone) return false;
    const int id = ccAt(app.pointerX - kCcRoom, app.pointerY - kCcRoom);
    if (id == CcBright) {
        changeBrightness(app, dir > 0 ? -3 : 3, -1);
    } else if (id == CcVol) {
        const int v = std::clamp(app.volume + (dir > 0 ? -app.cfg.volumeStep : app.cfg.volumeStep), 0, app.cfg.volumeMax);
        app.volume = v;
        runDetached({"wpctl", "set-volume", "-l", volLimit(app).c_str(), "@DEFAULT_AUDIO_SINK@", volStep(app, dir > 0).c_str()});
        showOsd(app, OsdVolume);  // (app.volume is already the new one, so the audio event would find no change and show nothing)
    } else {
        return false;
    }
    drawControlCenter(app);
    return true;
}
void ccRelease(App& app) {
    ControlCenter& C = app.cc;
    if (C.drag == CcVol) {
        const std::string pct = std::to_string(std::clamp(app.volume, 0, app.cfg.volumeMax)) + "%";
        runDetached({"wpctl", "set-volume", "-l", volLimit(app).c_str(), "@DEFAULT_AUDIO_SINK@", pct.c_str()});
    }
    C.drag = 0;
    drawControlCenter(app);
}
void ccClick(App& app) {
    ControlCenter& C = app.cc;
    const double x = app.pointerX - kCcRoom, y = app.pointerY - kCcRoom;
    if (popIsOut(C.anim)) return;
    if (!Rect{0, 0, kCcW, kCcH}.contains(x, y)) { retractControlCenter(app); return; }
    if (C.page != CcPageNone) { ccRadioClick(app); return; }
    const int id = ccAt(x, y);
    switch (id) {
        case CcPower: closeControlCenter(app); openPowerMenu(app); return;
        case CcSettings: closeControlCenter(app); toggleSettings(); return;
        case CcRestart: closeControlCenter(app); restartShell(); return;
        case CcWifi: if (C.wifi != "absent") ccOpenRadio(app, CcPageWifi); return;
        case CcBt: if (C.bt != "absent") ccOpenRadio(app, CcPageBt); return;
        case CcNight: if (C.night != "absent") { ccRunScript("toggle", "night"); } break;
        case CcPrev: mediaSend(app, {"previous"}); break;
        case CcPlay: mediaSend(app, {"play-pause"}); break;
        case CcNext: mediaSend(app, {"next"}); break;
        case CcAwake: app.cfg.keepAwake = app.cfg.keepAwake ? 0 : 1; saveConfig(app); setKeepAwake(app, app.cfg.keepAwake); break;
        case CcRec: quickAction(app, 1); drawAll(app); break;
        case CcShot: closeControlCenter(app); quickAction(app, 2); return;
        case CcMute: runDetached({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"}); break;
        case CcBright: case CcVol: C.drag = id; ccMotion(app); return;
        case CcUpdates: closeControlCenter(app); runUpdatesTerminal(app); return;
        case CcWall: closeControlCenter(app); toggleSelector(app); return;
        default: break;
    }
    drawControlCenter(app);
}
