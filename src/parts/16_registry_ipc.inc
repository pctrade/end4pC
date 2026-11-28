

// The connector name of a screen (wl_output v4). A screen that appears while the shell runs gets its bar here, once the name is known.
void onOutputName(void* d, wl_output*, const char* name) {
    auto* info = static_cast<OutInfo*>(d);
    info->connector = name ? name : "";
    if (info->app->ready) createBar(*info->app, info->output, info->name);
}
const wl_output_listener kOutputListener = {
    [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
    [](void* d, wl_output*, uint32_t flags, int32_t, int32_t height, int32_t) {
        auto* info = static_cast<OutInfo*>(d);
        if ((flags & WL_OUTPUT_MODE_CURRENT) && height > info->app->screenH) info->app->screenH = height;
    },
    [](void*, wl_output*) {},
    [](void*, wl_output*, int32_t) {},
    onOutputName,
    [](void*, wl_output*, const char*) {},
};

void onBlurCapabilities(void* d, ext_background_effect_manager_v1*, uint32_t flags) {
    static_cast<App*>(d)->blurCapable = (flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR) != 0;
}
// Ctrl + Tab / Ctrl + Shift + Tab: the next / previous tab of the popup that is open (the clock, weather, AI usage and resources popups, and the audio
// menu). The keys are bound in Hyprland only while one of them is open, and they are taken (the app below does not get them): always bound, they
// would break Ctrl + Tab in every browser and editor.
bool popupHasTabs(const App& app) {
    const ClockPanel& P = app.clockP;
    if (P.open() && P.phase < 2 && !(P.kind == 4 || P.kind == 5 || (P.kind == 2 && aiShown(app).size() < 2))) return true;
    return app.amenu.open() && app.amenu.configured;
}
void popupTabStep(App& app, int dir) {
    ClockPanel& P = app.clockP;
    const bool clockTabs = P.open() && P.phase < 2 && !(P.kind == 4 || P.kind == 5 || (P.kind == 2 && aiShown(app).size() < 2));
    if (clockTabs) {
        const int count = P.kind == 0 ? 3 : P.kind == 3 ? 4 : 2;
        clkSelectTab(P, (P.tab + dir + count) % count);
        P.adding = false;
        P.query.clear();
        drawClockPanel(app);
    } else if (app.amenu.open() && app.amenu.configured) {
        amSetTab(app, (app.amenu.tab + dir + 3) % 3);
    }
}
void popupKeysSync(App& app) {
    const bool want = wmKind() == Wm::Hyprland && popupHasTabs(app);
    if (want == app.popupKeysBound) return;
    app.popupKeysBound = want;
    char exe[512];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    const std::string e(exe);
    if (want) {
        captureArgv({"hyprctl", "eval", "hl.bind(\"CTRL + Tab\", hl.dsp.exec_cmd(\"" + e + " 'popuptab next'\"))"});
        captureArgv({"hyprctl", "eval", "hl.bind(\"CTRL + SHIFT + Tab\", hl.dsp.exec_cmd(\"" + e + " 'popuptab prev'\"))"});
    } else {
        captureArgv({"hyprctl", "eval", "hl.unbind(\"CTRL + Tab\")"});
        captureArgv({"hyprctl", "eval", "hl.unbind(\"CTRL + SHIFT + Tab\")"});
    }
}

// The toplevels the compositor lists, with their ids: the overview needs them to capture a window.
void onFtHandleClosed(void* d, ext_foreign_toplevel_handle_v1* h) {
    auto* app = static_cast<App*>(d);
    app->ftHandles.erase(std::remove_if(app->ftHandles.begin(), app->ftHandles.end(), [&](const FtHandle& f) { return f.h == h; }), app->ftHandles.end());
    ext_foreign_toplevel_handle_v1_destroy(h);
}
void onFtHandleDone(void*, ext_foreign_toplevel_handle_v1*) {}
void onFtHandleText(void*, ext_foreign_toplevel_handle_v1*, const char*) {}
void onFtHandleId(void* d, ext_foreign_toplevel_handle_v1* h, const char* id) {
    for (FtHandle& f : static_cast<App*>(d)->ftHandles)
        if (f.h == h) f.id = id;
}
const ext_foreign_toplevel_handle_v1_listener kFtHandleListener = {onFtHandleClosed, onFtHandleDone, onFtHandleText, onFtHandleText, onFtHandleId};
void onFtToplevel(void* d, ext_foreign_toplevel_list_v1*, ext_foreign_toplevel_handle_v1* h) {
    auto* app = static_cast<App*>(d);
    app->ftHandles.push_back({h, ""});
    ext_foreign_toplevel_handle_v1_add_listener(h, &kFtHandleListener, app);
}
void onFtFinished(void*, ext_foreign_toplevel_list_v1*) {}
const ext_foreign_toplevel_list_v1_listener kFtListListener = {onFtToplevel, onFtFinished};

const ext_background_effect_manager_v1_listener kBlurManagerListener = {onBlurCapabilities};

void onGlobal(void* d, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* app = static_cast<App*>(d);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, wp_viewporter_interface.name) == 0 && !hidpi::disabled()) {
        app->viewporter = static_cast<wp_viewporter*>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
    } else if (std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0 && !hidpi::disabled()) {
        app->fscaleMgr = static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(app->seat, &kSeatListener, app);
    } else if (std::strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        app->cursorMgr = static_cast<wp_cursor_shape_manager_v1*>(
            wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1));
    } else if (std::strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
        app->idleMgr = static_cast<zwp_idle_inhibit_manager_v1*>(wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
    } else if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0) {
        app->bgEffectManager = static_cast<ext_background_effect_manager_v1*>(
            wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, std::min(version, 1u)));
        ext_background_effect_manager_v1_add_listener(app->bgEffectManager, &kBlurManagerListener, app);
    } else if (std::strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
        app->ftList = static_cast<ext_foreign_toplevel_list_v1*>(wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1));
        ext_foreign_toplevel_list_v1_add_listener(app->ftList, &kFtListListener, app);
    } else if (std::strcmp(interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0) {
        app->ftSrcMgr = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1*>(wl_registry_bind(registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1));
    } else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
        app->copyCapMgr = static_cast<ext_image_copy_capture_manager_v1*>(wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        app->layerShell = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        const uint32_t ver = std::min(version, 4u);
        auto* output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, ver));
        auto info = std::make_unique<OutInfo>();
        info->app = app;
        info->output = output;
        info->name = name;
        wl_output_add_listener(output, &kOutputListener, info.get());
        app->outputs.push_back(std::move(info));
        // Outputs announced during the first roundtrip get their bar once layer-shell is bound. A screen that appears later waits for
        // its name (the bar needs it to know whether the screen is wanted); an old compositor never sends one.
        if (!app->ready) app->pendingOutputs.push_back({output, name});
        else if (ver < 4) createBar(*app, output, name);
    }
}

void onGlobalRemove(void* d, wl_registry*, uint32_t name) {
    auto* app = static_cast<App*>(d);
    for (auto& b : app->bars) {
        if (b->outputName == name) {
            Bar* bar = b.get();
            destroyBar(*bar);
            std::erase_if(app->bars, [&](const auto& x) { return x.get() == bar; });
            break;
        }
    }
    if (std::erase_if(app->outputs, [&](const auto& o) { return o->name == name; })) applyCorners(*app);
}

const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};

std::string runtimeDir() {
    const char* d = std::getenv("XDG_RUNTIME_DIR");
    return d ? d : "/tmp";
}

int unixAddr(sockaddr_un& a, const std::string& path) {
    a = {};
    a.sun_family = AF_UNIX;
    if (path.size() >= sizeof a.sun_path) return -1;
    std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
    return 0;
}

int listenSocket(const std::string& path) {
    sockaddr_un a;
    if (unixAddr(a, path) < 0) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    unlink(path.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

constexpr size_t kIpcRequestMax = 8192;
void handleClient(App& app, int listenFd) {
    int c = accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC);
    if (c < 0) return;
    timeval tv{0, 200000};
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[kIpcRequestMax + 1];
    size_t len = 0;
    ssize_t n = 0;
    while (len < kIpcRequestMax && (n = read(c, buf + len, kIpcRequestMax - len)) > 0) {
        len += static_cast<size_t>(n);
        if (std::memchr(buf, '\n', len)) break;
    }
    std::string cmd(buf, len);
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();

    std::string reply;
    if (cmd == "ping") reply = "pong";
    else if (cmd == "volume" || cmd.rfind("volume ", 0) == 0) {
        // volume [get|up|down|set N|mute|unmute|toggle]: the default speakers; the reply is the volume it ends at
        const std::string a = cmd.size() > 7 ? cmd.substr(7) : "get";
        const bool digits = a.size() > 4 && a.rfind("set ", 0) == 0 && a.find_first_not_of("0123456789", 4) == std::string::npos && a.size() <= 7;
        if (a == "up" || a == "down") {
            app.volume = std::clamp(app.volume + (a == "up" ? app.cfg.volumeStep : -app.cfg.volumeStep), 0, app.cfg.volumeMax);
            runDetached({"wpctl", "set-volume", "-l", volLimit(app).c_str(), "@DEFAULT_AUDIO_SINK@", volStep(app, a == "down").c_str()});
            showOsd(app, OsdVolume);  // (app.volume is already the new one, so the audio event would find no change and show nothing)
        } else if (digits) {
            app.volume = std::clamp(std::atoi(a.c_str() + 4), 0, app.cfg.volumeMax);
            runDetached({"wpctl", "set-volume", "-l", volLimit(app).c_str(), "@DEFAULT_AUDIO_SINK@", (std::to_string(app.volume) + "%").c_str()});
            showOsd(app, OsdVolume);
        } else if (a == "mute" || a == "unmute" || a == "toggle") {
            app.muted = a == "toggle" ? !app.muted : a == "mute";
            runDetached({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", a == "toggle" ? "toggle" : a == "mute" ? "1" : "0"});
            showOsd(app, OsdVolume);
        } else if (a != "get") {
            reply = "usage: volume [get|up|down|set N|mute|unmute|toggle]";
        }
        if (reply.empty()) reply = std::to_string(app.volume) + (app.muted ? " muted" : "");
    }
    else if (cmd == "usercard image" || cmd.rfind("usercard image ", 0) == 0) {
        std::string path = cmd.size() > 15 ? cmd.substr(15) : "";
        while (!path.empty() && path.back() == ' ') path.pop_back();
        if (path == "-") path.clear();
        if (!path.empty() && access(expandHome(path).c_str(), R_OK) != 0) reply = "no such file";
        else { userSetImage(app, path); reply = "ok"; }
    }
    else if (cmd.rfind("widget ", 0) == 0) {
        std::istringstream in(cmd.substr(7));
        std::string name, state;
        int w = 0, h = 0, l = 0, t = 0;
        in >> name >> state;
        const bool sized = static_cast<bool>(in >> w >> h >> l >> t);
        DeskWidget* found = nullptr;
        for (DeskWidget* d : deskList(app)) if (d->name == name) found = d;
        if (found && state == "edit") { if (found->open()) deskToggleEdit(app, *found); reply = "ok"; }
        else if (found && state == "move" && sized && found->open() && deskGlideTo(app, *found, std::clamp(w, 100, 1600), std::clamp(h, 60, 1200), std::clamp(l, 0, 4000), std::clamp(t, 0, 4000))) reply = "ok";
        else if (!found || (state != "on" && state != "off" && state != "move")) reply = "usage: widget NAME on|off|edit|move [W H LEFT TOP]";
        else {
            if (found->open()) deskClose(app, *found);  // ("move" of a widget that cannot glide opens it at the new place, like "on")
            if (sized) {
                found->w = std::clamp(w, 100, 1600);
                found->h = std::clamp(h, 60, 1200);
                found->ml = std::clamp(l, -1, 4000);
                found->mt = std::clamp(t, 0, 4000);
                found->pinned = found->ml >= 0;
            }
            if (state == "on" || state == "move") deskOpen(app, *found); else found->enabled = false;
            deskSave(*found);
            reply = "ok";
        }
    }
    else if (cmd == "applytheme" || cmd.rfind("applytheme ", 0) == 0) {
        const std::string path = cmd.size() > 11 ? cmd.substr(11) : "";
        applyTheme(app, path.empty() ? app.wallpaper : path);
        reply = "ok";
    }
    else if (cmd == "videosync") { videoPauseSync(app, true); reply = "ok"; }
    else if (cmd == "tzsync") { timeZoneSync(app); reply = "ok"; }
    else if (cmd.rfind("wallpaper ", 0) == 0) { changeWallpaper(app, cmd.substr(10)); reply = "ok"; }
    else if (cmd.rfind("workspace ", 0) == 0) { focusWorkspace(std::atoi(cmd.c_str() + 10)); reply = "ok"; }
    else if (cmd == "workspace") reply = std::to_string(app.workspace);
    else if (cmd == "music") reply = app.musicStatus + " / " + app.musicArtist + " / " + app.musicTitle + " / art=" + (app.musicArt ? "yes" : "no");
    else if (cmd.rfind("notifications ", 0) == 0) { app.notifCount = std::max(0, std::atoi(cmd.c_str() + 14)); drawAll(app); reply = "ok"; }
    else if (cmd.rfind("quick ", 0) == 0) { quickAction(app, std::atoi(cmd.c_str() + 6)); drawAll(app); reply = "ok"; }
    else if (cmd == "screenshot" || cmd.rfind("screenshot ", 0) == 0) {
        const std::string mode = cmd.size() > 11 ? cmd.substr(11) : "region";
        if (mode == "region") { openCapture(app); reply = "ok"; }
        else if (mode == "ocr" || mode == "lens" || mode == "qr") { openCapture(app, false, mode == "ocr" ? 1 : mode == "lens" ? 2 : 3); reply = "ok"; }
        else if (mode == "qrcode") reply = "usage: qr <text>";
        else if (mode == "screen" || mode == "window" || mode == "edit") { runScreenshot(app, mode.c_str()); reply = "ok"; }
        else reply = "usage: screenshot region|screen|window|edit|ocr|lens";
    }
    else if (cmd.rfind("network ", 0) == 0) {
        app.netKind = cmd.substr(8);
        drawAll(app);
        reply = "ok";
    }
    else if (cmd == "selector" || cmd.rfind("selector ", 0) == 0) {
        const std::string a = cmd.size() > 9 ? cmd.substr(9) : "toggle";
        if (a == "open") openSelector(app);
        else if (a == "close") retractSelector(app);
        else toggleSelector(app);
        reply = app.sel.open() && !popIsOut(app.sel.anim) ? "open" : "closed";
    }
    else if (cmd == "identify" || cmd.rfind("identify ", 0) == 0) {
        openIdentify(app, cmd.size() > 9 ? cmd.substr(9) : "");
        reply = std::to_string(g_idSurfs.size());
    }
    else if (cmd == "desktopmenu" || cmd.rfind("desktopmenu ", 0) == 0) {
        const std::string a = cmd.size() > 12 ? cmd.substr(12) : "open";
        if (a.rfind("open", 0) == 0) {
            double x = -1, y = -1;
            if (std::sscanf(a.c_str(), "open %lf %lf", &x, &y) == 2) { app.pointerX = x; app.pointerY = y; }
            Bar* bar = app.wallBar ? app.wallBar : (app.bars.empty() ? nullptr : app.bars[0].get());
            if (bar && !app.dmenu.open()) openDesktopMenu(app, bar);
        } else retractDesktopMenu(app);
        reply = app.dmenu.open() && !popIsOut(app.dmenu.anim) ? "open" : "closed";
    }
    else if (cmd == "audiomenu" || cmd.rfind("audiomenu ", 0) == 0) {
        const std::string a = cmd.size() > 10 ? cmd.substr(10) : "open";
        AudioMenu& A = app.amenu;
        if (a.rfind("open", 0) == 0) {
            double x = 1635, y = 22;
            std::sscanf(a.c_str(), "open %lf %lf", &x, &y);
            app.pointerX = x;
            app.pointerY = y;
            Bar* bar = app.hoverBar ? app.hoverBar : (app.bars.empty() ? nullptr : app.bars[0].get());
            if (bar && !A.open()) openAudioMenu(app, bar);
        } else if (a.rfind("tab ", 0) == 0) {
            const std::string t = a.substr(4);
            amSetTab(app, t == "input" ? AudioMenu::TabInput : t == "apps" ? AudioMenu::TabApps : AudioMenu::TabOutput);
        } else retractAudioMenu(app);
        reply = A.open() && !popIsOut(A.anim) ? "open tab=" + std::to_string(A.tab) : "closed";
    }
    else if (cmd == "shapes") {
        for (const auto& [name, nums] : materialShapes()) reply += (reply.empty() ? "" : " ") + name;
    }
    else if (cmd == "launcher" || cmd.rfind("launcher ", 0) == 0) {
        const std::string a = cmd.size() > 9 ? cmd.substr(9) : "toggle";
        if (a == "open") openLauncherOrOverview(app);
        else if (a == "close") retractLauncher(app);
        else if (a.rfind("query ", 0) == 0) {
            openLauncherOrOverview(app);
            app.launcher.text = a.substr(6);
            app.launcher.cursor = app.launcher.anchor = app.launcher.text.size();
            updateLauncherResults(app);
            drawLauncher(app);
        } else if (a == "clipboard" || a == "emoji") {
            const char pfx = a == "clipboard" ? kPfxClipboard : kPfxEmoji;
            Launcher& L = app.launcher;
            if (L.open() && !popIsOut(L.anim) && !L.text.empty() && L.text[0] == pfx) retractLauncher(app);
            else {
                openLauncherOrOverview(app);
                L.text = std::string(1, pfx);
                L.cursor = L.anchor = L.text.size();
                launcherTextChanged(app);
                drawLauncher(app);
            }
        } else if (a == "results") {
            for (const auto& r : app.launcher.results) reply += (reply.empty() ? "" : " | ") + r.type + ": " + r.name;
            if (reply.empty()) reply = "(none)";
            reply += '\n';
            (void)!write(c, reply.data(), reply.size());
            close(c);
            return;
        } else toggleLauncher(app);
        reply = app.launcher.open() && !popIsOut(app.launcher.anim) ? "open" : "closed";
    }
    else if (cmd.rfind("brightness", 0) == 0) {
        // brightness [up|down|set N] [output]: the monitor in focus, or the one named
        std::string a = cmd.size() > 11 ? cmd.substr(11) : "get", output;
        if (a.rfind("set ", 0) == 0) {
            const size_t sp = a.find(' ', 4);
            if (sp != std::string::npos) { output = a.substr(sp + 1); a.resize(sp); }
        } else if (const size_t sp = a.find(' '); sp != std::string::npos) { output = a.substr(sp + 1); a.resize(sp); }
        if (a == "up") changeBrightness(app, 5, -1, output);
        else if (a == "down") changeBrightness(app, -5, -1, output);
        else if (a.rfind("set ", 0) == 0) changeBrightness(app, 0, std::atoi(a.c_str() + 4), output);
        else brightnessFollowFocus(app);  // (get: reads the screen in focus when its value is not known yet; ask again a moment later)
        reply = app.brightFailed ? "the screen does not answer: install brightnessctl (laptop) or ddcutil (external monitor) and check you are in the video group"
                : app.brightness < 0 ? "unknown (asking the monitor)" : std::to_string(app.brightness);
    }
    else if (cmd == "conflicts") { const size_t n = openConflicts(app, true); reply = n ? std::to_string(n) + " conflicting program(s)" : "none"; }
    else if (cmd == "forcequit") { openForceQuit(app); reply = app.fq.surface ? "open" : "closed"; }
    else if (cmd == "osd volume") { showOsd(app, OsdVolume); reply = "ok"; }
    else if (cmd == "osd brightness") { showOsd(app, OsdBrightness); reply = "ok"; }
    else if (cmd == "notifs") {
        for (const auto& n : app.notifs)
            reply += "#" + std::to_string(n.id) + " app=[" + n.app + "] icon=[" + n.icon + "] entry=[" + n.desktopEntry + "] -> " + notifSolarIcon(n) + "\n";
        (void)!write(c, reply.data(), reply.size());
        close(c);
        return;
    }
    else if (cmd == "panel" || cmd.rfind("panel ", 0) == 0) {
        const std::string a = cmd.size() > 6 ? cmd.substr(6) : "toggle";
        if (a == "open") openNotifPanel(app);
        else if (a == "close") retractNotifPanel(app);
        else toggleNotifPanel(app);
        reply = app.panel.open() && !popIsOut(app.panel.anim) ? "open" : "closed";
    }
    else if (cmd == "clockpanel" || cmd.rfind("clockpanel ", 0) == 0) {
        const std::string a = cmd.size() > 11 ? cmd.substr(11) : "toggle";
        if (clkIpcTab(app, a)) {}
        else if (a == "open") openClockPanel(app, nullptr);
        else if (a == "close") retractClockPanel(app);
        else toggleClockPanel(app);
        reply = app.clockP.open() && app.clockP.phase < 2 ? "open" : "closed";
    }
    else if (cmd == "weatherpanel" || cmd.rfind("weatherpanel ", 0) == 0) {
        const std::string a = cmd.size() > 13 ? cmd.substr(13) : "toggle";
        if (clkIpcTab(app, a)) {}
        else if (a == "open") { if (app.clockP.open() && app.clockP.kind != 1) closeClockPanel(app); openClockPanel(app, nullptr, 1); }
        else if (a == "close") retractClockPanel(app);
        else toggleClockPanel(app, nullptr, 1);
        reply = app.clockP.open() && app.clockP.kind == 1 && app.clockP.phase < 2 ? "open" : "closed";
    }
    else if (cmd == "aipanel" || cmd.rfind("aipanel ", 0) == 0) {
        const std::string a = cmd.size() > 8 ? cmd.substr(8) : "toggle";
        if (clkIpcTab(app, a)) {}
        else if (a == "close") retractClockPanel(app);
        else if (a == "open") { if (app.clockP.open() && app.clockP.kind != 2) closeClockPanel(app); openClockPanel(app, nullptr, 2); }
        else toggleClockPanel(app, nullptr, 2);
        reply = app.clockP.open() && app.clockP.kind == 2 && app.clockP.phase < 2 ? "open" : "closed";
    }
    else if (cmd == "respanel" || cmd.rfind("respanel ", 0) == 0) {
        const std::string a = cmd.size() > 9 ? cmd.substr(9) : "toggle";
        if (clkIpcTab(app, a)) {}
        else if (a == "close") retractClockPanel(app);
        else if (a == "open") { if (app.clockP.open() && app.clockP.kind != 3) closeClockPanel(app); openClockPanel(app, nullptr, 3, 1); }
        else toggleClockPanel(app, nullptr, 3, 1);
        reply = app.clockP.open() && app.clockP.kind == 3 && app.clockP.phase < 2 ? "open" : "closed";
    }
    else if (cmd == "batterypanel" || cmd.rfind("batterypanel ", 0) == 0) {
        const std::string a = cmd.size() > 13 ? cmd.substr(13) : "toggle";
        if (a == "close") retractClockPanel(app);
        else if (a == "open") { if (app.clockP.open() && app.clockP.kind != 4) closeClockPanel(app); openClockPanel(app, nullptr, 4); }
        else toggleClockPanel(app, nullptr, 4);
        reply = app.clockP.open() && app.clockP.kind == 4 && app.clockP.phase < 2 ? "open" : "closed";
    }
    else if (cmd == "media" || cmd.rfind("media ", 0) == 0) {
        const std::string a = cmd.size() > 6 ? cmd.substr(6) : "toggle";
        if (a == "open") openMediaPanel(app);
        else if (a == "close") retractMediaPanel(app);
        else toggleMediaPanel(app);
        reply = app.media.open() && !popIsOut(app.media.anim) ? "open" : "closed";
    }
    else if ([&] { for (DeskWidget* w : deskList(app)) if (cmd == w->name || cmd.rfind(std::string(w->name) + " ", 0) == 0) return true; return false; }()) {  // the desktop widgets (todo, weather, player): show | hide | toggle
        DeskWidget* d = nullptr;
        for (DeskWidget* w : deskList(app)) if (cmd == w->name || cmd.rfind(std::string(w->name) + " ", 0) == 0) d = w;
        const size_t sp = cmd.find(' ');
        const std::string a = sp == std::string::npos ? "toggle" : cmd.substr(sp + 1);
        if (a == "show") deskOpen(app, *d);
        else if (a == "hide") deskClose(app, *d);
        else if (d->open()) deskClose(app, *d);
        else deskOpen(app, *d);
        reply = d->open() ? "shown" : "hidden";
    }
    else if (cmd == "widgets" || cmd.rfind("widgets ", 0) == 0) {
        const std::string a = cmd.size() > 8 ? cmd.substr(8) : "";
        const bool now = app.cfg.widgetsLocked != 0;
        const bool to = a == "lock" ? true : a == "unlock" ? false : !now;
        const std::string err = setConfig(app, "widgetsLocked", to ? "1" : "0");
        if (err.empty()) { applyConfig(app); drawAll(app); }
        reply = err.empty() ? (app.cfg.widgetsLocked ? "locked" : "unlocked") : err;
    }
    else if (cmd == "lock") { startLock(); reply = "ok"; }
    else if (cmd == "control" || cmd.rfind("control ", 0) == 0) {
        const std::string a = cmd.size() > 8 ? cmd.substr(8) : "toggle";
        if (a == "open") openControlCenter(app);
        else if (a == "close") retractControlCenter(app);
        else if (a == "wifi" || a == "bt") ccOpenPage(app, a == "wifi" ? CcPageWifi : CcPageBt);
        else toggleControlCenter(app);
        reply = app.cc.open() && !popIsOut(app.cc.anim) ? "open" : "closed";
    }
    else if (cmd == "switcher" || cmd.rfind("switcher ", 0) == 0) {
        const std::string a = cmd.size() > 9 ? cmd.substr(9) : "open";
        if (a == "next") swStep(app, 1, false);
        else if (a == "prev") swStep(app, -1, false);
        else if (a == "open") swStep(app, 1, true);
        else if (a == "commit") swCommit(app);
        else if (a == "close") retractSwitcher(app);
        else if (a == "status") {  // how many of the windows are captured and how many frames came (for a test)
            long frames = 0;
            int live = 0;
            for (const auto& c : app.swLive.wins) { frames += c->frames; live += c->hasFrame ? 1 : 0; }
            const SwLayout l = swLayout(app.sw);
            reply = "panel=" + std::to_string(static_cast<int>(l.px)) + "," + std::to_string(static_cast<int>(l.py)) + " " + std::to_string(static_cast<int>(l.pw)) + "x" + std::to_string(static_cast<int>(l.ph)) + " cols=" + std::to_string(l.cols) + " visCols=" + std::to_string(l.visCols) + " row=" + (l.row ? "y" : "n") + " ";
            reply += "windows=" + std::to_string(app.swLive.wins.size()) + " live=" + std::to_string(live) + " frames=" + std::to_string(frames) + " ";
        }
        reply += app.sw.open() && !popIsOut(app.sw.anim) ? "open" : "closed";
    }
    else if (cmd == "power" || cmd.rfind("power ", 0) == 0) {
        const std::string a = cmd.size() > 6 ? cmd.substr(6) : "toggle";
        if (a == "open") openPowerMenu(app);
        else if (a == "close") retractPowerMenu(app);
        else togglePowerMenu(app);
        reply = (app.power.open() && !popIsOut(app.power.anim)) || (app.ic.open() && g_icPowerMode && !popIsOut(app.ic.anim)) ? "open" : "closed";
    }
    else if (cmd == "updates" || cmd == "updates check") {
        startUpdatesNow(app);
        reply = "ok";
    }
    else if (cmd == "settings" || cmd.rfind("settings ", 0) == 0) {
        toggleSettings();
        reply = "ok";
    }
    else if (cmd == "play3r" || cmd.rfind("play3r ", 0) == 0) {
        togglePlay3r(cmd.size() > 7 ? cmd.substr(7) : "");
        reply = "ok";
    }
    else if (cmd == "fil3s" || cmd.rfind("fil3s ", 0) == 0) {
        openFil3s(cmd.size() > 6 ? cmd.substr(6) : "");
        reply = "ok";
    }
    else if (cmd == "t3xt" || cmd.rfind("t3xt ", 0) == 0) {
        openT3xt(cmd.size() > 5 ? cmd.substr(5) : "");
        reply = "ok";
    }
    else if (cmd == "vid3os" || cmd.rfind("vid3os ", 0) == 0) {
        openVid3os(cmd.size() > 7 ? cmd.substr(7) : "");
        reply = "ok";
    }
    else if (cmd == "imag3s" || cmd.rfind("imag3s ", 0) == 0) {
        openImag3s(cmd.size() > 7 ? cmd.substr(7) : "");
        reply = "ok";
    }
    else if (cmd == "tools" || cmd.rfind("tools ", 0) == 0) {
        toggleTools();
        reply = "ok";
    }
    else if (cmd == "tasks" || cmd.rfind("tasks ", 0) == 0) {
        toggleTasks();
        reply = "ok";
    }
    else if (cmd == "dnd" || cmd.rfind("dnd ", 0) == 0) {
        const std::string a = cmd.size() > 4 ? cmd.substr(4) : "toggle";
        setDoNotDisturb(app, a == "on" ? true : a == "off" ? false : !app.cfg.doNotDisturb);
        reply = app.cfg.doNotDisturb ? "on" : "off";
    }
    else if (cmd == "magnifier" || cmd.rfind("magnifier ", 0) == 0) reply = magnifierCommand(app, cmd.size() > 10 ? cmd.substr(10) : "get");
    else if (cmd.rfind("timer", 0) == 0 || cmd.rfind("stopwatch", 0) == 0 || cmd.rfind("reminder", 0) == 0) {
        const size_t sp = cmd.find(' ');
        reply = timersCommand(app, cmd.substr(0, sp), sp == std::string::npos ? "" : cmd.substr(sp + 1));
        timersCheck(app);
    }
    else if (cmd == "osk" || cmd.rfind("osk ", 0) == 0) {
        const std::string a = cmd.size() > 4 ? cmd.substr(4) : "toggle";
        if (a == "open") openOsk(app); else if (a == "close") closeOsk(app); else toggleOsk(app);
        drawAll(app);  // (the quick action lights while it is open)
        reply = app.osk.surface ? "open" : "closed";
    }
    else if (cmd.rfind("qr ", 0) == 0) { openQrCard(app, cmd.substr(3)); reply = app.qr.surface ? "open" : "failed"; }
    else if (cmd == "overview status") {
        reply = "hoverPlus=" + std::string(app.ov.hoverPlus ? "y" : "n") + " hoverWs=" + std::to_string(app.ov.hoverWs) + " seen=" + std::to_string(app.ov.dragSeen) + " armed=" + (app.ov.dragArmed ? "y" : "n") + " onOv=" + (app.onOv ? "y" : "n") + " win=" + app.ov.dragWin + " drag=" + std::to_string(static_cast<int>(app.ov.dragX)) + "," + std::to_string(static_cast<int>(app.ov.dragY)) + " ptr=" + std::to_string(static_cast<int>(app.pointerX)) + "," + std::to_string(static_cast<int>(app.pointerY)) + "\n" + "handles=" + std::to_string(app.ftHandles.size()) + " src=" + (app.ftSrcMgr ? "y" : "n") + " cap=" + (app.copyCapMgr ? "y" : "n") + "\n";
        for (const auto& w : app.ov.wins) reply += "win id=" + w->handleId + " ses=" + (w->ses ? "y" : "n") + " sized=" + (w->sized ? "y" : "n") + " dead=" + (w->dead ? "y" : "n") + " frame=" + (w->hasFrame ? "y" : "n") + " fails=" + std::to_string(w->fails) + " reason=" + std::to_string(w->lastReason) + " buf=" + std::to_string(w->bw) + "x" + std::to_string(w->bh) + " fmt=" + std::to_string(w->fmt) + "\n";
        for (const FtHandle& f : app.ftHandles) reply += "  handle " + f.id + "\n";
    }
    else if (cmd == "popuptab next" || cmd == "popuptab prev") { popupTabStep(app, cmd == "popuptab next" ? 1 : -1); reply = "ok"; }
    else if (cmd == "overview close") { closeOverview(app); reply = "closed"; }
    else if (cmd == "overview") { openOverview(app); reply = app.ov.surface && !app.ov.closing ? "open" : "closed"; }
    else if (cmd.rfind("privacy", 0) == 0) {  // privacy [mic|cam|screen ...|off]: preview the indicators by hand (no argument: the state now)
        PrivacyBar& P = app.privacy;
        std::string arg = cmd.size() > 8 ? cmd.substr(8) : "";
        if (!arg.empty()) {
            P.test = arg != "off";
            P.mic = P.test && arg.find("mic") != std::string::npos ? "preview" : "";
            P.cam = P.test && arg.find("cam") != std::string::npos ? "preview" : "";
            P.screen = P.test && arg.find("screen") != std::string::npos ? "preview" : "";
            drawAll(app);
        }
        reply = (P.mic.empty() ? "" : "mic: " + P.mic + "\n") + (P.cam.empty() ? "" : "cam: " + P.cam + "\n") + (P.screen.empty() ? "" : "screen: " + P.screen + "\n");
        if (reply.empty()) reply = "nothing in use";
    }
    else if (cmd == "drives") {  // opens the removable drives popup at its bar button (for a key bind or a test)
        Bar* bar = mainBar(app);
        if (bar && !sideBar(app) && hasSlot(*bar, "drives") && drivesVisible(app)) {
            app.pointerX = slotX(*bar, "drives") + kQuickBox / 2.0 + app.cfg.frame;
            app.pointerY = bar->height / 2.0;
            toggleDrivesMenu(app, bar);
            reply = app.drivesMenu.open() ? "open" : "closed";
        } else reply = "no drives";
    }
    else if (cmd == "visualizer" || cmd.rfind("visualizer ", 0) == 0) deskVizToggle(app, cmd.size() > 11 ? cmd.substr(11) : "toggle", reply);
    else if (cmd == "game" || cmd.rfind("game ", 0) == 0) {
        const std::string a = cmd.size() > 5 ? cmd.substr(5) : "toggle";
        if (a == "open") openGame(app); else if (a == "close") retractGame(app); else toggleGame(app);
        reply = app.game.surface && !app.game.closing ? "open" : "closed";
    }
    else if (cmd == "recording") reply = app.recording ? "recording" : "idle";
    else if (cmd == "config") reply = "workspaces=" + std::to_string(app.cfg.workspaces) + " frame=" + std::to_string(app.cfg.frame) + " doNotDisturb=" + std::to_string(app.cfg.doNotDisturb) + " notificationTimeout=" + std::to_string(app.cfg.notificationTimeout) + " networkAlways=" + std::to_string(app.cfg.networkAlways) + " barOpacity=" + std::to_string(app.cfg.barOpacity) + " left=" + joinQuoted(app.cfg.layoutLeft) + " center=" + joinQuoted(app.cfg.layoutCenter) + " right=" + joinQuoted(app.cfg.layoutRight) + " barPosition=" + app.cfg.barPosition + " barStyle=" + app.cfg.barStyle + " workspaceStyle=" + app.cfg.workspaceStyle + " colorScheme=" + app.cfg.colorScheme + " wallpaperTransition=" + app.cfg.wallpaperTransition + " wallpaperTransitionMs=" + std::to_string(app.cfg.wallpaperTransitionMs) + " colorMode=" + app.cfg.colorMode + " selectorShape=" + app.cfg.selectorShape + " notificationShape=" + app.cfg.notificationShape + " preferredPlayer=" + app.cfg.preferredPlayer + " iconStyle=" + app.cfg.iconStyle + " uiBackground=" + app.cfg.uiBackground;
    else if (cmd == "collage" || cmd.rfind("collage ", 0) == 0) reply = collageCommand(app, cmd.size() > 8 ? cmd.substr(8) : "");
    else if (cmd == "dock" || cmd.rfind("dock ", 0) == 0) {
        const std::string a = cmd.size() > 5 ? cmd.substr(5) : "toggle";
        const int want = a == "on" ? 1 : a == "off" ? 0 : app.cfg.dock ? 0 : 1;
        const std::string err = setConfig(app, "dock", std::to_string(want));
        if (err.empty()) applyConfig(app);
        reply = err.empty() ? (app.cfg.dock ? "on" : "off") : err;
    }
    else if (cmd.rfind("set ", 0) == 0) {
        const size_t sp = cmd.find(' ', 4);
        const std::string key = sp == std::string::npos ? "" : cmd.substr(4, sp - 4), val = sp == std::string::npos ? "" : cmd.substr(sp + 1);
        const std::string err = sp == std::string::npos ? "usage: set <key> <value>"
                              : (key == "selectorShape" || key == "notificationShape" || key == "centeredWallpaperShape") && !materialShapes().count(val) ? "unknown shape (try: shapes)"
                                                                                       : setConfig(app, key, val);
        if (err.empty()) applyConfig(app);
        if (err.empty() && key.compare(0, 6, "border") == 0) applyBorders(app);
        if (err.empty() && (key == "colorMode" || key == "colorScheme" || key == "accent" || key == "theme" || key.compare(0, 5, "theme") == 0) && !app.wallpaper.empty())
            runPalette(app, app.wallpaper);
        reply = err.empty() ? "ok" : err;
    }
    else if (cmd == "tray") {
        reply = std::to_string(app.tray.size()) + " items:";
        for (const auto& t : app.tray) reply += " [" + t.id + " | " + t.title + " | " + t.status + " | " + t.iconName + (t.pw ? " +pixmap" : "") + (t.isMenu ? " menu" : "") + "]";
    }
    else if (cmd == "tray match") {
        for (const auto& t : app.tray) {
            const TrayWindow w = trayFindWindow(app, t);
            reply += "[" + t.id + " -> " + (w.rank < 0 ? std::string("no window") : w.cls + " " + w.id + " by " + w.how + (w.rank == 0 ? " (focused)" : "")) + "] ";
        }
    }
    else if (cmd.rfind("tray click ", 0) == 0) { trayClick(app, std::atoi(cmd.c_str() + 11), false); reply = "ok"; }
    else if (cmd == "tray open" || cmd == "tray close") { app.trayOpen = cmd == "tray open"; drawAll(app); reply = "ok"; }
    else if (cmd == "accentpick") { pickAccent(); reply = "ok"; }
    else if (cmd.rfind("periph ", 0) == 0) {
        App::Periph d;
        char st[32] = "", kd[32] = "";
        int n = std::sscanf(cmd.c_str() + 7, "%d %31s %31s", &d.pct, st, kd);
        app.periph.clear();
        if (n >= 3) { d.state = st; d.kind = kd; d.name = cmd.size() > 7 + 1 ? "Test device" : ""; app.periph.push_back(d); }
        drawAll(app);
        reply = app.periph.empty() ? "no devices" : "device " + std::to_string(d.pct) + "% " + d.state;
    }
    else if (cmd.rfind("battery", 0) == 0) {
        const std::string arg = cmd.size() > 8 ? cmd.substr(8) : "";
        app.batFake = arg == "off" ? "" : arg;
        updateBattery(app);
        drawAll(app);
        reply = app.batPresent ? "battery " + std::to_string(app.batPct) + (app.batCharging ? " charging" : "") : "no battery";
    }
    else if (cmd == "quit") { app.running = false; reply = "bye"; }
    else if (cmd == "restart") { restartShell(); reply = "restarting"; }
    else reply = "unknown command";
    reply += '\n';
    (void)!write(c, reply.data(), reply.size());
    close(c);
}

// ---- the video wallpaper stops when nobody sees it (scripts/video_wallpaper.sh, mpvpaper) -------------------------------------------------------
// A stopped mpvpaper (SIGSTOP) keeps its last picture on the screen and costs nothing. Stops: every screen has a fullscreen window (Hyprland),
// the computer runs on battery, the lock screen is up. askCovered: ask the compositor again (after the events that can change it).
bool videoWallpaperOn() { return access((stateDir() + "/video-wallpaper.txt").c_str(), F_OK) == 0; }
bool lockScreenUp() {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    std::ifstream f(std::string(rt ? rt : "/tmp") + "/end4pC-locked");
    int pid = 0;
    return (f >> pid) && pid > 0 && kill(pid, 0) == 0;
}
// The time zone of the computer is what /etc/localtime points to (timedatectl swaps it). localtime_r keeps the zone it first read, so after a change
// tzset() has to be called: then the bar, the clocks of the desktop and the calendar show the new time without restarting the shell.
bool timeZoneSync(App& app) {
    static std::string last;
    char buf[PATH_MAX];
    const ssize_t n = readlink("/etc/localtime", buf, sizeof buf - 1);
    std::string now = n > 0 ? std::string(buf, static_cast<size_t>(n)) : "";
    if (n <= 0) {  // (a copy of the file instead of a link: its modification time says)
        struct stat st{};
        if (stat("/etc/localtime", &st) == 0) now = "file:" + std::to_string(static_cast<long long>(st.st_mtime));
    }
    if (last.empty()) { last = now; return false; }
    if (now == last) return false;
    last = now;
    tzset();
    app.clockTime.clear();
    updateClock(app);
    drawAll(app);
    return true;
}

void videoPauseSync(App& app, bool askCovered) {
    if (!videoWallpaperOn()) { app.videoPaused = false; return; }
    if (askCovered && app.cfg.videoPauseFullscreen && wmKind() == Wm::Hyprland)
        app.videoCovered = captureArgv({assetsDir() + "/../scripts/video_wallpaper.sh", "covered"}).rfind("1", 0) == 0;
    const bool onBattery = app.cfg.videoPauseBattery && app.batPresent && !app.batCharging;
    const bool covered = app.cfg.videoPauseFullscreen && app.videoCovered;
    const bool want = onBattery || covered || lockScreenUp();
    if (want == app.videoPaused) return;
    app.videoPaused = want;
    videoWallpaper(want ? "pause" : "resume");
}

int hyprEventSocket() {
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) return -1;
    sockaddr_un a;
    if (unixAddr(a, runtimeDir() + "/hypr/" + sig + "/.socket2.sock") < 0) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    return fd;
}

// niri's IPC socket in event-stream mode: one request ("EventStream"), then a JSON line per event for as long as it stays open.
int niriEventSocket() {
    const char* path = std::getenv("NIRI_SOCKET");
    if (!path || !*path) return -1;
    sockaddr_un a;
    if (unixAddr(a, path) < 0) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    static const char kRequest[] = "\"EventStream\"\n";
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 ||
        write(fd, kRequest, sizeof kRequest - 1) != static_cast<ssize_t>(sizeof kRequest - 1)) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

int swayEventFd() {
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return -1;
    if (fork() == 0) {
        childSignalReset();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        dup2(p[1], STDOUT_FILENO);
        const int nul = open("/dev/null", O_WRONLY);
        dup2(nul, STDERR_FILENO);
        execlp("swaymsg", "swaymsg", "-r", "-m", "-t", "subscribe", "[\"workspace\",\"window\",\"input\"]", nullptr);
        _exit(127);
    }
    close(p[1]);
    return p[0];
}

// The event socket of the running compositor, or -1: Hyprland's socket2, or niri's event stream (sway: a pipe from swaymsg).
int wmEventSocket() {
    if (wmKind() == Wm::Sway) return swayEventFd();
    return wmKind() == Wm::Niri ? niriEventSocket() : hyprEventSocket();
}

bool readWmEvents(App& app, int fd, std::string& pending) {
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof buf);
    if (n == 0) return false;
    if (n < 0) return errno == EAGAIN || errno == EINTR;
    pending.append(buf, n);
    size_t nl;
    bool changed = false;
    WmEvents ev;
    while ((nl = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, nl);
        pending.erase(0, nl + 1);
        wmClassifyEvent(line, ev);
    }
    if (ev.workspace > 0 && ev.workspace != app.workspace) { app.workspace = ev.workspace; changed = true; }
    if (ev.workspace > 0 || ev.monitors || ev.occupied)
        changed = refreshMonitorWorkspaces(app) || changed;
    if (app.ov.surface && changed) ovWorkspaceChanged(app);
    if (app.ov.surface && (ev.occupied || ev.window)) ovSoon(app);
    if (ev.covered && app.dv.surface) deskVizCheckCovered(app);
    if (ev.covered) videoPauseSync(app, true);
    if (ev.reread) {
        const int id = wmActiveWorkspace();
        if (id > 0 && id != app.workspace) { app.workspace = id; changed = true; }
    }
    if (ev.occupied || ev.window || ev.focus) {  // the apps of the bar's dock panel
        const bool had = !app.dtp.items.empty();
        dtpMarkDirty(app);
        if (inBarLayout(app, "docktopanel") || had) changed = true;
    }
    if (ev.occupied) {
        const std::set<int> before = app.occupied;
        const int countBefore = app.wsCount;
        const size_t closedBefore = app.closedSpecial;
        refreshOccupied(app);
        changed = changed || before != app.occupied || countBefore != app.wsCount || closedBefore != app.closedSpecial;
        if (refreshWsApps(app)) changed = true;
        dockOnWindowsChanged(app);
    }
    if (app.cfg.dock && app.cfg.dockAutoHide == 2 && (changed || ev.workspace > 0)) dockSmartUpdate(app);
    if (!ev.occupied && ev.focus && app.cfg.workspaceApps && refreshWsApps(app)) changed = true;
    if (ev.window && inBarLayout(app, "activewindow") && awinRead(app)) changed = true;
    if (ev.keyboard && kbdRead(app)) {
        oskSync(app);
        changed = true;
        if (app.clockP.open() && app.clockP.kind == 5) drawClockPanel(app);
    }
    if (changed) drawAll(app);
    return true;
}
