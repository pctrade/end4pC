

constexpr double kMediaCardW = 420, kMediaCardH = 140, kMediaRoom = 20;
constexpr double kMediaLyricsH = 269, kMediaLyricsArea = 150, kMediaEqH = 400;
constexpr int kMediaSurfW = static_cast<int>(kMediaCardW + 2 * kMediaRoom), kMediaSurfH = static_cast<int>(kMediaEqH + 50 + 2 * kMediaRoom);
constexpr double kMediaPad = 13, kMediaArt = 114, kMediaGap = 15, kMediaRadius = 19;
constexpr double kMediaBtn = 24, kMediaPlay = 44, kMediaRowGap = 5;
constexpr int kMediaPollMs = 500, kMediaFrameMs = 33;

void closeMediaPanel(App& app);
void drawMediaPanel(App& app);
void lyricsSvcGet(std::vector<LyricLine>& lines, std::string& status, uint64_t* version);
void lyricsSvcRequest(const std::string& title, const std::string& artist, double duration, const std::string& provider);
void lyricsSvcEnsure(const std::string& title, const std::string& artist, double duration, const std::string& provider);
uint64_t lyricsSvcVersion();

Color mixColor(const Color& a, const Color& b, double p) {
    return {p * a.r + (1 - p) * b.r, p * a.g + (1 - p) * b.g, p * a.b + (1 - p) * b.b};
}

struct Hsl { double h, s, l; };

Hsl toHsl(const Color& c) {
    const double mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), l = (mx + mn) / 2, d = mx - mn;
    if (d < 1e-9) return {0, 0, l};
    const double s = d / (1 - std::fabs(2 * l - 1));
    double h;
    if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.0);
    else if (mx == c.g) h = (c.b - c.r) / d + 2;
    else h = (c.r - c.g) / d + 4;
    h /= 6;
    if (h < 0) h += 1;
    return {h, s, l};
}

Color fromHsl(const Hsl& v) {
    const double c = (1 - std::fabs(2 * v.l - 1)) * v.s, hp = v.h * 6, x = c * (1 - std::fabs(std::fmod(hp, 2.0) - 1)), m = v.l - c / 2;
    double r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    return {r + m, g + m, b + m};
}

Color adaptToAccent(const Color& c1, const Color& c2) {
    const Hsl a = toHsl(c1), b = toHsl(c2);
    return fromHsl({b.h, b.s, a.l});
}

MediaColors computeMediaColors(const App& app, const Color& dom) {
    MediaColors m;
    const Color background = app.color("background"), primary = app.color("primary"), onBackground = app.color("on_background");
    const Color layer1Base = app.color("surface_container_low"), onVariant = app.color("on_surface_variant");
    const Color secCont = app.color("secondary_container"), onSecCont = app.color("on_secondary_container"), onPrimary = app.color("on_primary");
    const bool domDark = toHsl(dom).l < 0.5, darkMode = app.cfg.colorMode == "dark";
    const Color layer0Base = mixColor(background, primary, 0.99);
    m.layer0 = mixColor(layer0Base, dom, (domDark && darkMode) ? 0.6 : 0.5);
    m.layer1 = mixColor(layer1Base, dom, 0.5);
    m.onLayer0 = mixColor(onBackground, dom, 0.5);
    m.subtext = mixColor(onVariant, dom, 0.5);
    m.primary = mixColor(adaptToAccent(primary, dom), dom, 0.5);
    const Color hoverBase = mixColor(layer1Base, onVariant, 0.92), activeBase = mixColor(layer1Base, onVariant, 0.85);
    m.primaryHover = mixColor(adaptToAccent(mixColor(primary, hoverBase, 0.87), dom), dom, 0.3);
    m.primaryActive = mixColor(adaptToAccent(mixColor(primary, activeBase, 0.7), dom), dom, 0.3);
    m.secondaryContainer = mixColor(secCont, dom, 0.15);
    m.secondaryContainerHover = mixColor(mixColor(secCont, onSecCont, 0.90), dom, 0.3);
    m.secondaryContainerActive = mixColor(mixColor(secCont, onSecCont, 0.54), dom, 0.5);
    m.onPrimary = mixColor(adaptToAccent(onPrimary, dom), dom, 0.5);
    m.onSecondaryContainer = mixColor(onSecCont, dom, 0.5);
    return m;
}

std::string mediaCoverPath(const App& app) {
    const std::string& url = app.musicArtUrl;
    if (url.rfind("file://", 0) == 0) return percentDecode(url.substr(7));
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        const std::string path = artCachePath(url);
        return access(path.c_str(), R_OK) == 0 ? path : "";
    }
    return !url.empty() && url[0] == '/' ? url : "";
}

cairo_surface_t* loadSquare(const std::string& path, int size) {
    int iw = 0, ih = 0;
    if (path.empty() || !gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) return nullptr;
    const double scale = std::max(double(size) / iw, double(size) / ih);
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::max(size, int(iw * scale + 0.5)), std::max(size, int(ih * scale + 0.5)), FALSE, nullptr);
    if (!pb) return nullptr;
    cairo_surface_t* img = surfaceFromPixbuf(pb);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = mkcr(out);
    cairo_set_source_surface(cr, img, (size - gdk_pixbuf_get_width(pb)) / 2.0, (size - gdk_pixbuf_get_height(pb)) / 2.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(img);
    g_object_unref(pb);
    return out;
}

Color averageColor(cairo_surface_t* img) {
    cairo_surface_flush(img);
    const int w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img), stride = cairo_image_surface_get_stride(img);
    const unsigned char* px = cairo_image_surface_get_data(img);
    double r = 0, g = 0, b = 0;
    for (int y = 0; y < h; y++) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(px + y * stride);
        for (int x = 0; x < w; x++) {
            r += (row[x] >> 16) & 255;
            g += (row[x] >> 8) & 255;
            b += row[x] & 255;
        }
    }
    const double n = std::max(1, w * h) * 255.0;
    return {r / n, g / n, b / n};
}

cairo_surface_t* makeBlurredCover(cairo_surface_t* cover) {
    constexpr int bw = 42, bh = 14;
    cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh);
    cairo_t* cr = mkcr(small);
    const double s = double(bw) / cairo_image_surface_get_width(cover);
    cairo_translate(cr, 0, (bh - bw) / 2.0);
    cairo_scale(cr, s, s);
    cairo_set_source_surface(cr, cover, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(small);
    unsigned char* px = cairo_image_surface_get_data(small);
    const int stride = cairo_image_surface_get_stride(small);
    for (int pass = 0; pass < 2; pass++) {
        std::vector<unsigned char> copy(px, px + stride * bh);
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++)
                for (int ch = 0; ch < 4; ch++) {
                    int sum = 0, n = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            const int xx = std::clamp(x + dx, 0, bw - 1), yy = std::clamp(y + dy, 0, bh - 1);
                            sum += copy[yy * stride + xx * 4 + ch];
                            n++;
                        }
                    px[y * stride + x * 4 + ch] = static_cast<unsigned char>(sum / n);
                }
    }
    cairo_surface_mark_dirty(small);
    return small;
}

void refreshMediaCover(App& app) {
    MediaPanel& M = app.media;
    if (M.cover) { cairo_surface_destroy(M.cover); M.cover = nullptr; }
    if (M.blur) { cairo_surface_destroy(M.blur); M.blur = nullptr; }
    M.cover = loadSquare(mediaCoverPath(app), 150);
    Color dom = app.color("secondary_container");
    if (M.cover) {
        M.blur = makeBlurredCover(M.cover);
        dom = mixColor(averageColor(M.cover), app.color("primary_container"), 0.8);
    }
    M.col = computeMediaColors(app, dom);
    M.coverKey = app.musicArtUrl;
}

void refreshMediaCoverIfNeeded(App& app) {
    // A remote image can finish downloading after the popup cached this URL with
    // no surface. Retry when the bar's artwork is ready, even if the URL is unchanged.
    if (app.media.coverKey != app.musicArtUrl || (!app.media.cover && app.musicArt)) refreshMediaCover(app);
}

std::vector<mediaplayer::Snapshot> mediaSnapshots(const std::string& instance) {
    std::vector<mediaplayer::Snapshot> players;
    if (instance.empty()) return players;
    int output[2];
    if (pipe2(output, O_CLOEXEC) < 0) return players;
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        dup2(output[1], STDOUT_FILENO);
        const int nul = open("/dev/null", O_WRONLY);
        dup2(nul, STDERR_FILENO);
        constexpr const char* format = "{{playerInstance}}\t{{position}}\t{{mpris:length}}\t{{volume}}\t{{status}}\t{{kde:pid}}\t{{mpris:artUrl}}\t{{xesam:url}}\t{{title}}";
        if (instance == "plasma-browser-integration")
            execlp("playerctl", "playerctl", "-a", "metadata", "--format", format, nullptr);
        else execlp("playerctl", "playerctl", "-a", "-p", instance.c_str(), "metadata", "--format", format, nullptr);
        _exit(127);
    }
    close(output[1]);
    if (pid < 0) { close(output[0]); return players; }
    fcntl(output[0], F_SETFL, O_NONBLOCK);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    std::string pending;
    bool complete = false;
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd fd{output[0], POLLIN, 0};
        const int left = std::max(1, int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count()));
        const int ready = poll(&fd, 1, left);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) break;
        char buf[4096];
        ssize_t n;
        while ((n = read(output[0], buf, sizeof buf)) > 0) {
            pending.append(buf, n);
            size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos) {
                mediaplayer::Snapshot snapshot;
                if (mediaplayer::parse(pending.substr(0, newline), snapshot)) players.push_back(std::move(snapshot));
                pending.erase(0, newline + 1);
            }
            if (std::chrono::steady_clock::now() >= deadline) break;
        }
        if (n > 0) break;  // output kept arriving until the deadline
        if (n == 0) { complete = true; break; }
        if (errno != EAGAIN && errno != EINTR) break;
    }
    close(output[0]);
    if (!complete) kill(pid, SIGKILL);
    // SIGCHLD is ignored in the shell, but tests/command children reap normally.
    // Bound cleanup too, in case a helper closes stdout without exiting.
    pid_t reaped;
    while ((reaped = waitpid(pid, nullptr, WNOHANG)) == 0 && std::chrono::steady_clock::now() < deadline) poll(nullptr, 0, 1);
    if (reaped == 0) {
        kill(pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    }
    // A partial list cannot safely establish an unambiguous browser pair.
    if (!complete) players.clear();
    return players;
}

void pollMedia(App& app) {
    MediaPanel& M = app.media;
    // playerctl's -p also matches NAME.*. Read all such matches and select the
    // exact instance below, so a prefix peer cannot hide the displayed player.
    const auto players = mediaSnapshots(app.musicPlayer);
    M.polledAt = std::chrono::steady_clock::now();
    M.nextPoll = M.polledAt + std::chrono::milliseconds(kMediaPollMs);
    const auto* metadata = mediaplayer::find(players, app.musicPlayer, app.musicTitle);
    if (!metadata) {
        M.position = M.length = 0;
        M.status.clear();
        app.musicPlaybackPlayer.clear();
        return;
    }
    const auto* playback = mediaplayer::playback(players, *metadata);
    app.musicPlaybackPlayer = playback->instance;
    if (M.drag != 1) M.position = playback->position;
    M.length = playback->duration;
    // Keep the bridge's HTML volume control, even when native MediaSession
    // provides a more accurate song timeline and transport controls.
    if (M.drag != 2 && metadata->volume) M.volume = std::clamp(*metadata->volume, 0.0, 1.0);
    M.status = playback->status;
    // Bridges may create the file or publish its URL without a metadata signal.
    // A periodic poll retries a missing cover without requiring pause/play.
    const bool hadArt = app.musicArt != nullptr;
    const std::string oldArt = app.musicArtUrl;
    const std::string cover = mediaplayer::coverUrl(*metadata);
    app.musicArtFallback = metadata->artwork.empty() ? cover : "";
    updateCover(app, cover);
    if (oldArt != app.musicArtUrl || hadArt != (app.musicArt != nullptr)) drawAll(app);
}

double mediaPositionNow(const MediaPanel& M) {
    double pos = M.position;
    if (M.status == "Playing" && M.drag != 1) pos += std::chrono::duration<double>(std::chrono::steady_clock::now() - M.polledAt).count();
    return M.length > 0 ? std::clamp(pos, 0.0, M.length) : pos;
}

void startCava(App& app) {
    MediaPanel& M = app.media;
    if (M.cavaPid > 0) return;
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return;
    const std::string config = assetsDir() + "/../scripts/cava/raw_output_config.txt";
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        dup2(p[1], STDOUT_FILENO);
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDERR_FILENO);
        execlp("cava", "cava", "-p", config.c_str(), nullptr);
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return; }
    M.cavaPid = pid;
    M.cavaFd = p[0];
    M.cavaBuf.clear();
}

bool barVizWanted(const App& app) { return inBarLayout(app, "visualizer") || (islandActive(app) && islandShowsMusic(app)); }
bool vizWanted(const App& app) { return app.musicStatus == "Playing" && (barVizWanted(app) || deskVizWanted(app)); }

void killCava(App& app) {
    MediaPanel& M = app.media;
    if (M.cavaPid > 0) kill(M.cavaPid, SIGTERM);
    if (M.cavaFd >= 0) close(M.cavaFd);
    M.cavaPid = 0;
    M.cavaFd = -1;
    M.points.clear();
    deskVizIdle(app);
}

void stopCava(App& app) {
    if (!vizWanted(app)) killCava(app);
}

void syncCava(App& app) {
    if (vizWanted(app)) { startCava(app); return; }
    if (!(app.media.open() && app.media.status == "Playing")) { killCava(app); app.vizShown.clear(); }
}

void wavyLine(cairo_t* cr, double x0, double x1, double cy, double lineWidth, double amplitudeMul, double freq, double fullLength, double phase) {
    const double amp = lineWidth * amplitudeMul;
    cairo_set_line_width(cr, lineWidth);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_new_path(cr);
    bool first = true;
    for (double x = x0 + lineWidth / 2; x <= x1 - lineWidth / 2; x += 1) {
        const double y = cy + amp * std::sin(freq * 2 * M_PI * (x - x0) / fullLength + phase);
        if (first) { cairo_move_to(cr, x, y); first = false; }
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
}

std::string cleanMusicTitle(std::string t) {
    auto strip = [&](char open, char close) {
        size_t i = 0;
        while (i < t.size() && t[i] == ' ') i++;
        if (i < t.size() && t[i] == open) {
            const size_t e = t.find(close, i);
            if (e != std::string::npos) t = " " + t.substr(e + 1);
        }
    };
    strip('(', ')'); strip('[', ']'); strip('{', '}');
    return trimmed(t);
}

std::string friendlyTime(double seconds) {
    if (!(seconds >= 0)) return "0:00";
    const int s = static_cast<int>(seconds);
    char buf[24];
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60);
    else std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

double mediaWidgetCentre(const Bar& bar, bool vertical) {
    const App& app = *bar.app;
    if (vertical) {
        for (const auto& item : sideItems(bar))
            if (item.id == HitMedia) return item.r.y + item.r.h / 2;
    } else if (hasSlot(bar, "media") && mediaBarVisible(app)) {
        return musicX(bar) + moduleWidth(app, bar, "media") / 2;
    }
    return -1;
}

void mediaFont(PangoLayout* layout, double px, bool bold = false) {
    PangoFontDescription* fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
}

double mediaEaseOutBack(double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double c1 = 1.70158, c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
}

void mediaStepLyricSpring(MediaPanel& M, std::chrono::steady_clock::time_point now) {
    const double target = M.lyricsMode || M.eqMode ? 1.0 : 0.0;
    const double dt = M.lyricLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.05, std::chrono::duration<double>(now - M.lyricLast).count());
    M.lyricLast = now;
    constexpr double kStiffness = 180, kDamping = 12;
    M.lyricV += (kStiffness * (target - M.lyricT) - kDamping * M.lyricV) * dt;
    M.lyricT += M.lyricV * dt;
    M.lyricMoving = std::fabs(target - M.lyricT) > 0.001 || std::fabs(M.lyricV) > 0.01;
    if (!M.lyricMoving) { M.lyricT = target; M.lyricV = 0; }
}

void mediaStepEqSpring(MediaPanel& M, std::chrono::steady_clock::time_point now) {
    const double target = M.eqMode ? 1.0 : 0.0;
    const double dt = M.eqLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.05, std::chrono::duration<double>(now - M.eqLast).count());
    M.eqLast = now;
    constexpr double kStiffness = 180, kDamping = 12;
    M.eqV += (kStiffness * (target - M.eqT) - kDamping * M.eqV) * dt;
    M.eqT += M.eqV * dt;
    M.eqMoving = std::fabs(target - M.eqT) > 0.001 || std::fabs(M.eqV) > 0.01;
    if (!M.eqMoving) { M.eqT = target; M.eqV = 0; }
}

void mediaUpdateLyrics(App& app, std::chrono::steady_clock::time_point now) {
    MediaPanel& M = app.media;
    if (!M.lyricsMode && M.lyricT < 0.01) return;
    if (M.lyricsMode && !app.musicTitle.empty() && !app.musicArtist.empty()) lyricsSvcEnsure(app.musicTitle, app.musicArtist, M.length, app.cfg.lyricsProvider);
    if (const uint64_t v = lyricsSvcVersion(); v != M.lyricSeen) {
        M.lyricSeen = v;
        lyricsSvcGet(M.lyrics, M.lyricStatus, nullptr);
        M.lyricActive = -2;
    }
    int idx = -1;
    const double pos = mediaPositionNow(M) + 0.1;
    for (size_t i = 0; i < M.lyrics.size() && M.lyrics[i].time <= pos; i++) idx = static_cast<int>(i);
    if (idx != M.lyricActive) { M.lyricActive = idx; M.lyricChange = now; }
}

void mediaPaintLyrics(cairo_t* cr, PangoLayout* layout, const App& app, MediaPanel& M, const MediaColors& C, double x, double y, double w, double h, double alpha,
                      std::chrono::steady_clock::time_point now) {
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    const bool ok = M.lyricStatus == "ok" && !M.lyrics.empty();
    if (!ok) {
        const double cx = x + w / 2, cy = y + h / 2 - 6;
        M.rLyricsRetry = {cx - 24, cy - 24, 48, 48};
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, 24, 0, 2 * M_PI);
        setSource(cr, C.secondaryContainer, alpha);
        cairo_fill(cr);
        if (M.lyricStatus == "none") {
            drawIcon(cr, "music_note", cx - 12, cy - 12, 24, C.onSecondaryContainer, true);
            mediaFont(layout, 11);
            pango_layout_set_text(layout, i18n::tr("No lyrics found"), -1);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            setSource(cr, C.subtext, alpha);
            cairo_move_to(cr, cx - tw / 2.0, cy + 32);
            pango_cairo_show_layout(cr, layout);
        } else {
            const double phase = std::chrono::duration<double>(now.time_since_epoch()).count() * 5.0;
            cairo_set_line_width(cr, 4);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx, cy, 11, phase, phase + 1.5 * M_PI);
            setSource(cr, C.primary, alpha);
            cairo_stroke(cr);
        }
        cairo_restore(cr);
        return;
    }
    M.rLyricsRetry = {};
    struct Slot { int dist; int idx; double hgt; };
    std::vector<Slot> slots;
    const int n = static_cast<int>(M.lyrics.size());
    constexpr double kGap = 6;
    constexpr double kSize[3] = {16, 14, 12.5};
    constexpr double kFade[4] = {1.0, 0.6, 0.35, 0.15};
    pango_layout_set_width(layout, static_cast<int>(w * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    auto lineText = [&](const Slot& s) { const std::string& t = M.lyrics[s.idx].text; return t.empty() && s.dist == 0 && M.lyricActive >= 0 ? std::string("♪") : t; };
    for (int d = -3; d <= 3; d++) {
        const int i = M.lyricActive + d;
        if (i < 0 || i >= n) continue;
        Slot s{std::abs(d), i, 0};
        mediaFont(layout, kSize[std::min(2, s.dist)], s.dist == 0);
        pango_layout_set_ellipsize(layout, s.dist == 0 ? PANGO_ELLIPSIZE_NONE : PANGO_ELLIPSIZE_END);
        pango_layout_set_height(layout, -1);
        pango_layout_set_text(layout, lineText(s).c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        s.hgt = th;
        slots.push_back(s);
    }
    auto total = [&] { double t = 0; for (const Slot& s : slots) t += s.hgt; return slots.empty() ? 0.0 : t + kGap * (slots.size() - 1); };
    while (slots.size() > 1 && total() > h) {
        size_t far = 0;
        for (size_t i = 0; i < slots.size(); i++) if (slots[i].dist >= slots[far].dist) far = i;
        slots.erase(slots.begin() + far);
    }
    const double u = std::chrono::duration<double>(now - M.lyricChange).count() / 0.42;
    const double slide = 28 * (1 - mediaEaseOutBack(u));
    double yy = y + (h - total()) / 2 + slide;
    for (const Slot& s : slots) {
        mediaFont(layout, kSize[std::min(2, s.dist)], s.dist == 0);
        pango_layout_set_ellipsize(layout, s.dist == 0 ? PANGO_ELLIPSIZE_NONE : PANGO_ELLIPSIZE_END);
        pango_layout_set_height(layout, -1);
        pango_layout_set_text(layout, lineText(s).c_str(), -1);
        setSource(cr, s.dist == 0 ? C.primary : C.onLayer0, alpha * kFade[std::min(3, s.dist)]);
        cairo_move_to(cr, x, yy);
        pango_cairo_show_layout(cr, layout);
        yy += s.hgt + kGap;
    }
    pango_layout_set_width(layout, -1);
    pango_layout_set_height(layout, -1);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    cairo_restore(cr);
}

void mediaApplyInput(App& app, double cardTop) {
    MediaPanel& M = app.media;
    if (!M.open()) return;
    const int pinned = app.mediaPinned ? 1 : 0, h = static_cast<int>(M.cardH + 0.5);
    if (pinned == M.inputPinned && (!pinned || h == M.inputH)) return;
    if (pinned != M.inputPinned)
        zwlr_layer_surface_v1_set_keyboard_interactivity(M.layer, pinned ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    if (pinned) {
        wl_region* region = wl_compositor_create_region(app.compositor);
        wl_region_add(region, M.cardX, static_cast<int>(cardTop), static_cast<int>(kMediaCardW), h);
        wl_surface_set_input_region(M.surface, region);
        wl_region_destroy(region);
    } else {
        wl_surface_set_input_region(M.surface, nullptr);
    }
    M.inputPinned = pinned;
    M.inputH = h;
}

int eqMatchPreset(const EqState& E) {
    for (int i = 0; i < kEqPresetCount; i++) {
        bool same = true;
        for (int b = 0; b < kEqBands; b++) same = same && std::fabs(E.gain[b] - kEqPresets[i].gain[b]) < 0.01;
        if (same) return i;
    }
    return -1;
}

// The EQ page, drawn over the art and the lyrics: a switch, the presets, and a slider for the preamp and for each band.
void mediaDrawEq(App& app, cairo_t* cr, PangoLayout* layout, const MediaColors& C, double cw, double ch, double a) {
    MediaPanel& M = app.media;
    EqState& E = app.eq;
    eqLoad(E);
    const double x0 = kMediaPad, w = cw - 2 * kMediaPad, top = kMediaPad, bottom = ch - 100;
    roundedRect(cr, 6, 6, cw - 12, bottom - 6, kMediaRadius - 4);
    setSource(cr, C.layer0, 0.985 * a);
    cairo_fill(cr);
    auto text = [&](const std::string& s, double px, bool bold, double x, double y, const Color& c, double alpha, double boxW = 0, int align = 0) {
        mediaFont(layout, px, bold);
        pango_layout_set_text(layout, s.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, c, alpha);
        cairo_move_to(cr, align == 1 ? x + (boxW - tw) / 2 : align == 2 ? x + boxW - tw : x, y - th / 2.0);
        pango_cairo_show_layout(cr, layout);
    };
    text(i18n::tr("Equalizer"), 16, true, x0 + 2, top + 12, C.onLayer0, a);
    for (Rect& r : M.eqBand) r = {};
    for (Rect& r : M.eqChip) r = {};
    M.rEqToggle = {};
    if (!eqAvailable()) {
        pango_layout_set_width(layout, static_cast<int>((w - 20) * PANGO_SCALE));
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        text(i18n::tr("Install EasyEffects to use the equalizer"), 14, false, x0 + 10, (top + bottom) / 2, C.subtext, a);
        pango_layout_set_width(layout, -1);
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
        return;
    }
    const Rect sw{x0 + w - 46, top, 46, 24};
    M.rEqToggle = sw;
    roundedRect(cr, sw.x, sw.y, sw.w, sw.h, 12);
    setSource(cr, E.enabled ? C.primary : C.secondaryContainer, a);
    cairo_fill(cr);
    const double kx = E.enabled ? sw.x + sw.w - 12 - 3 : sw.x + 12 + 3;
    cairo_new_sub_path(cr);
    cairo_arc(cr, kx, sw.y + sw.h / 2, M.eqHover == 300 ? 8.5 : 7.5, 0, 2 * M_PI);
    setSource(cr, E.enabled ? C.onPrimary : C.onSecondaryContainer, a);
    cairo_fill(cr);

    const double gap = 6, chipW = (w - 3 * gap) / 4, chipH = 26, chipTop = top + 34;
    for (int i = 0; i < kEqPresetCount; i++) {
        const Rect r{x0 + (i % 4) * (chipW + gap), chipTop + (i / 4) * (chipH + gap), chipW, chipH};
        M.eqChip[i] = r;
        const bool on = E.preset == i && E.enabled, hot = M.eqHover == 200 + i;
        roundedRect(cr, r.x, r.y, r.w, r.h, chipH / 2);
        setSource(cr, on ? (hot ? C.primaryHover : C.primary) : (hot ? C.secondaryContainerHover : C.secondaryContainer), a);
        cairo_fill(cr);
        text(i18n::tr(kEqPresets[i].name), 12, true, r.x, r.y + r.h / 2, on ? C.onPrimary : C.onSecondaryContainer, a, r.w, 1);
    }

    const double sTop = chipTop + 2 * chipH + gap + 18, sBottom = bottom - 22, mid = (sTop + sBottom) / 2, half = (sBottom - sTop) / 2;
    const double colW = w / (kEqBands + 1);
    for (int k = 0; k <= kEqBands; k++) {
        const int id = k == 0 ? kEqBands : k - 1;
        const double v = id == kEqBands ? E.preamp : E.gain[id], cx = x0 + colW * (k + 0.5);
        M.eqBand[id] = {cx - colW / 2, sTop - 10, colW, sBottom - sTop + 20};
        const bool hot = M.eqHover == 100 + id || M.eqDrag == id;
        const double hy = mid - v / kEqRange * half;
        roundedRect(cr, cx - 2, sTop, 4, sBottom - sTop, 2);
        setSource(cr, C.secondaryContainer, a);
        cairo_fill(cr);
        setSource(cr, C.onLayer0, 0.3 * a);
        cairo_rectangle(cr, cx - 7, mid - 0.5, 14, 1);
        cairo_fill(cr);
        if (std::fabs(v) > 0.01) {
            roundedRect(cr, cx - 2, std::min(mid, hy), 4, std::fabs(hy - mid), 2);
            setSource(cr, C.primary, (E.enabled ? 1.0 : 0.5) * a);
            cairo_fill(cr);
        }
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, hy, hot ? 8 : 6.5, 0, 2 * M_PI);
        setSource(cr, E.enabled ? C.primary : C.subtext, a);
        cairo_fill(cr);
        text(id == kEqBands ? i18n::tr("Pre") : kEqFreqLabel[id], 10, false, cx - colW / 2, sBottom + 14, hot ? C.onLayer0 : C.subtext, a, colW, 1);
        if (hot) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%+.1f", v);
            text(buf, 11, true, cx - colW / 2, std::max(sTop - 10.0, hy - 17), C.onLayer0, a, colW, 1);
        }
    }
}

void drawMediaPanel(App& app) {
    MediaPanel& M = app.media;
    if (!M.open() || !M.configured) return;
    refreshMediaCoverIfNeeded(app);
    const auto now = std::chrono::steady_clock::now();
    mediaStepLyricSpring(M, now);
    mediaStepEqSpring(M, now);
    mediaUpdateLyrics(app, now);
    const double t = std::clamp(M.lyricT, -0.08, 1.35), eqT = std::clamp(M.eqT, -0.08, 1.35), eqA = std::clamp(M.eqT, 0.0, 1.0);
    const auto lerp = [&](double a, double b) { return a + (b - a) * t; };
    const int W = kMediaSurfW, H = kMediaSurfH;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, M.scale.factor(), "end4pC-media")) return;
    cairo_surface_t* surface = hf.surface;
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    popGroupBegin(cr);

    const bool havePlayer = !app.musicStatus.empty();
    const double cw = kMediaCardW;
    const double ch = havePlayer ? std::max(110.0, lerp(kMediaCardH, kMediaLyricsH) + eqT * (kMediaEqH - kMediaLyricsH)) : kMediaCardH;
    M.cardH = ch;
    const double cx = M.cardX;
    const double cy = M.side.length > 0 ? barPopupSideCardY(M.side, ch)
                                      : M.anchoredBottom ? H - kMediaRoom - ch : kMediaRoom;
    if (M.shadow && M.shadowH != static_cast<int>(ch)) { cairo_surface_destroy(M.shadow); M.shadow = nullptr; }
    if (!M.shadow) { M.shadow = makeShadowMask(cw, ch, kMediaRadius); M.shadowH = static_cast<int>(ch); }
    setSource(cr, app.color("shadow"), 0.45);
    cairo_mask_surface(cr, M.shadow, cx - kLaunShadow, cy - kLaunShadow + 2);
    cairo_translate(cr, cx, cy);
    M.originX = cx;
    M.originY = cy;
    mediaApplyInput(app, cy);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto font = [&](double px, bool bold = false) { mediaFont(layout, px, bold); };

    if (!havePlayer) {
        roundedRect(cr, 0, 0, cw, ch, kMediaRadius);
        setSource(cr, app.color("background"));
        cairo_fill(cr);
        font(17, true);
        pango_layout_set_text(layout, i18n::tr("No active player"), -1);
        int titleW = 0, titleH = 0;
        pango_layout_get_pixel_size(layout, &titleW, &titleH);
        font(13);
        pango_layout_set_text(layout, i18n::tr("Make sure your player has MPRIS support"), -1);
        pango_layout_set_width(layout, static_cast<int>(cw - 40) * PANGO_SCALE);
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        int detailH = 0;
        pango_layout_get_pixel_size(layout, nullptr, &detailH);
        constexpr double badge = 44, icon = 22, badgeGap = 10, textGap = 6;
        const double top = (ch - badge - badgeGap - titleH - textGap - detailH) / 2;
        shapePath(cr, "Cookie9Sided", (cw - badge) / 2, top, badge);
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
        drawIcon(cr, "music_note", (cw - icon) / 2, top + (badge - icon) / 2,
                 icon, app.color("on_secondary_container"), true);

        const double titleY = top + badge + badgeGap;
        setSource(cr, app.color("outline"));
        cairo_move_to(cr, 20, titleY + titleH + textGap);
        pango_cairo_show_layout(cr, layout);

        pango_layout_set_width(layout, -1);
        font(17, true);
        pango_layout_set_text(layout, i18n::tr("No active player"), -1);
        setSource(cr, app.color("on_background"));
        cairo_move_to(cr, (cw - titleW) / 2, titleY);
        pango_cairo_show_layout(cr, layout);
        M.rPrev = M.rPlay = M.rNext = M.rArt = M.rSlider = M.rPin = M.rLyrics = M.rLyricsRetry = M.rEq = M.rEqToggle = {};
    } else {
        const MediaColors& C = M.col;
        roundedRect(cr, 0, 0, cw, ch, kMediaRadius);
        cairo_save(cr);
        ClipGroup cutCard;  // base, blurred cover, wash and wave in one group; the curve cuts it once (no fringe of the layers below)
        beginClipGroup(cr, cutCard);
        setSource(cr, C.layer0);
        cairo_paint(cr);
        if (M.blur) {
            cairo_scale(cr, cw / cairo_image_surface_get_width(M.blur), ch / cairo_image_surface_get_height(M.blur));
            cairo_set_source_surface(cr, M.blur, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
            cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
            cairo_paint(cr);
            cairo_identity_matrix(cr);
        }
        setSource(cr, C.layer0, 0.7);
        cairo_paint(cr);

        if (M.status == "Playing" && M.points.size() >= 2) {
            const int n = static_cast<int>(M.points.size());
            std::vector<double> smooth(n);
            for (int i = 0; i < n; i++) {
                double sum = 0;
                int cnt = 0;
                for (int j = -2; j <= 2; j++) { sum += M.points[std::clamp(i + j, 0, n - 1)]; cnt++; }
                smooth[i] = sum / cnt;
            }
            const double over = 24, span = cw + 2 * over;
            cairo_move_to(cr, -over, ch + over);
            for (int i = 0; i < n; i++) cairo_line_to(cr, -over + i * span / (n - 1), ch + over - std::clamp(smooth[i] / 1000.0, 0.0, 1.0) * (ch + over));
            cairo_line_to(cr, cw + over, ch + over);
            cairo_close_path(cr);
            setSource(cr, C.primary, 0.15);
            cairo_fill(cr);
        }
        endClipGroup(cr, cutCard);
        cairo_restore(cr);

        const double artS = lerp(kMediaArt, 150), artR = lerp(14, 16);
        const double ax = kMediaPad, ay = kMediaPad;
        M.rArt = {ax, ay, artS, artS};
        cairo_save(cr);
        roundedRect(cr, ax, ay, artS, artS, artR);
        ClipGroup cutArt;
        beginClipGroup(cr, cutArt);
        setSource(cr, C.layer1, 0.5);
        cairo_paint(cr);
        if (M.cover) {
            cairo_translate(cr, ax, ay);
            cairo_scale(cr, artS / 150.0, artS / 150.0);
            cairo_set_source_surface(cr, M.cover, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            cairo_identity_matrix(cr);
        }
        const bool artHover = (M.hover == 4 || M.drag == 2) && t < 0.5;
        if (artHover) {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
            cairo_paint(cr);
        }
        endClipGroup(cr, cutArt);
        cairo_restore(cr);
        if (artHover) {
            const double dcx = ax + artS / 2, dcy = ay + artS / 2, R = artS / 2 - 14 - 2;
            cairo_new_sub_path(cr);
            cairo_arc(cr, dcx, dcy, R, 0, 2 * M_PI);
            setSource(cr, C.secondaryContainer);
            cairo_set_line_width(cr, 4);
            cairo_stroke(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, dcx, dcy, R, -M_PI / 2, -M_PI / 2 + 2 * M_PI * M.volume);
            setSource(cr, C.primary);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_stroke(cr);
            const double ang = -M_PI / 2 + 2 * M_PI * M.volume;
            cairo_new_sub_path(cr);
            cairo_arc(cr, dcx + R * std::cos(ang), dcy + R * std::sin(ang), 5, 0, 2 * M_PI);
            cairo_fill(cr);
            const char* vicon = M.volume <= 0 ? "volume_off" : M.volume < 0.5 ? "volume_down" : "volume_up";
            drawIcon(cr, vicon, dcx - 8, dcy - 8, 16, C.onLayer0, true);
        }

        const double lyricAlpha = std::clamp((t - 0.35) / 0.65, 0.0, 1.0) * (1 - eqA);
        if (lyricAlpha > 0.01) {
            const double lx = kMediaPad + artS + kMediaGap;
            mediaPaintLyrics(cr, layout, app, M, C, lx, kMediaPad, cw - lx - kMediaPad, kMediaLyricsArea, lyricAlpha, now);
        } else {
            M.rLyricsRetry = {};
        }
        if (eqA > 0.01) mediaDrawEq(app, cr, layout, C, cw, ch, eqA);
        else { M.rEqToggle = {}; for (Rect& r : M.eqBand) r = {}; for (Rect& r : M.eqChip) r = {}; }

        const double rowH = std::max(kMediaBtn, 24.0), rowY = ch - lerp(13, 12) - rowH;
        const double textX = lerp(kMediaPad + kMediaArt + kMediaGap, kMediaPad), textW = cw - textX - kMediaPad - lerp(0, kMediaPlay + 8);
        int tw = 0, th = 0, ah = 0, tmh = 0;
        const std::string title = cleanMusicTitle(app.musicTitle);
        font(17);
        pango_layout_set_text(layout, (title.empty() ? "Untitled" : title).c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        font(12);
        pango_layout_set_text(layout, "Ag", -1);
        pango_layout_get_pixel_size(layout, &tw, &ah);
        font(15);
        pango_layout_set_text(layout, "0:00", -1);
        pango_layout_get_pixel_size(layout, &tw, &tmh);
        const double titleY = lerp(kMediaPad, ch - 12 - rowH - tmh - 2 - (ah - 2) - th);
        const double artistY = titleY + th + lerp(2, -2), timeY = rowY - tmh - lerp(5, 0);
        pango_layout_set_width(layout, static_cast<int>(std::max(0.0, textW) * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        font(17, true);
        pango_layout_set_text(layout, (title.empty() ? "Untitled" : title).c_str(), -1);
        setSource(cr, C.onLayer0);
        cairo_move_to(cr, textX, titleY);
        pango_cairo_show_layout(cr, layout);
        font(12, true);
        pango_layout_set_text(layout, app.musicArtist.c_str(), -1);
        setSource(cr, C.subtext);
        cairo_move_to(cr, textX, artistY);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        const double pos = mediaPositionNow(M);
        font(15);
        pango_layout_set_text(layout, (friendlyTime(pos) + " / " + friendlyTime(M.length)).c_str(), -1);
        setSource(cr, C.subtext);
        cairo_move_to(cr, textX, timeY);
        pango_cairo_show_layout(cr, layout);

        auto flatButton = [&](double bx, double by, bool hot, bool on, const char* icon) {
            if (hot || on) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, bx + kMediaBtn / 2, by + kMediaBtn / 2, kMediaBtn / 2, 0, 2 * M_PI);
                setSource(cr, hot ? C.secondaryContainerHover : C.secondaryContainer);
                cairo_fill(cr);
            }
            drawIcon(cr, icon, bx + (kMediaBtn - 20) / 2, by + (kMediaBtn - 20) / 2, 20, on ? C.primary : C.onSecondaryContainer, true);
        };
        const double by = rowY + (rowH - kMediaBtn) / 2;
        const double lyricsX = cw - kMediaPad - kMediaBtn, pinX = lyricsX - kMediaRowGap - kMediaBtn, nextX = pinX - kMediaRowGap - kMediaBtn, prevX = textX + kMediaBtn + kMediaRowGap;
        M.rPrev = {prevX, by, kMediaBtn, kMediaBtn};
        M.rNext = {nextX, by, kMediaBtn, kMediaBtn};
        M.rPin = {pinX, by, kMediaBtn, kMediaBtn};
        M.rLyrics = {lyricsX, by, kMediaBtn, kMediaBtn};
        flatButton(prevX, by, M.hover == 1, false, "skip_previous");
        flatButton(nextX, by, M.hover == 3, false, "skip_next");
        flatButton(pinX, by, M.hover == 6, app.mediaPinned, "push_pin");
        flatButton(lyricsX, by, M.hover == 7, M.lyricsMode, "boombox/lyrics");
        M.rEq = {textX, by, kMediaBtn, kMediaBtn};
        flatButton(textX, by, M.hover == 8, M.eqMode, "tuning-2/tune");

        const double sx = prevX + kMediaBtn + kMediaRowGap, sw = std::max(0.0, nextX - kMediaRowGap - sx), sy = rowY, sh = rowH;
        M.rSlider = {sx, sy, sw, sh};
        const double v = M.length > 0 ? std::clamp(pos / M.length, 0.0, 1.0) : 0;
        const double pad = 4, eff = sw - 2 * pad, hx = sx + pad + v * eff, mid = sy + sh / 2;
        const double handleW = M.drag == 1 ? 1.5 : 3;
        const double phase = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() * 1000.0 / 400.0;
        setSource(cr, C.primary);
        if (hx - handleW / 2 - 4 > sx + pad) {
            wavyLine(cr, sx + pad, hx - handleW / 2 - 4, mid, 4, M.status == "Playing" ? 0.5 : 0, 6, sw, phase);
        }
        roundedRect(cr, hx - handleW / 2, mid - 12, handleW, 24, handleW / 2);
        setSource(cr, C.primary);
        cairo_fill(cr);
        const double tx0 = hx + handleW / 2 + 4, tx1 = sx + sw - pad;
        if (tx1 > tx0) {
            roundedRect(cr, tx0, mid - 2, tx1 - tx0, 4, 2);
            setSource(cr, C.secondaryContainer);
            cairo_fill(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, tx1 - 1.5, mid, 1.5, 0, 2 * M_PI);
            setSource(cr, C.primary);
            cairo_fill(cr);
        }

        const bool playing = M.status == "Playing";
        const double px = cw - kMediaPad - kMediaPlay, pyy = rowY - kMediaPlay - 5;
        M.rPlay = {px, pyy, kMediaPlay, kMediaPlay};
        roundedRect(cr, px, pyy, kMediaPlay, kMediaPlay, playing ? 17 : kMediaPlay / 2);
        setSource(cr, playing ? (M.hover == 2 ? C.primaryHover : C.primary) : (M.hover == 2 ? C.secondaryContainerHover : C.secondaryContainer));
        cairo_fill(cr);
        drawIcon(cr, playing ? "pause" : "play_arrow", px + (kMediaPlay - 18) / 2, pyy + (kMediaPlay - 18) / 2, 18,
                 playing ? C.onPrimary : C.onSecondaryContainer, true);
    }

    g_object_unref(layout);
    const double rev = std::max(0.0, M.anim.reveal);
    if (rev > 0.001) {
        double gox = 0, goy = 0;
        popGrowOrigin(app, cx, cy, cw, ch, M.along, gox, goy);
        popGroupEnd(cr, rev, gox, goy, M.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8));
    } else {
        cairo_pattern_destroy(cairo_pop_group(cr));
    }
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    hidpi::present(M.surface, buffer, hf, M.scale);
}

void resetMediaPanel(MediaPanel& M) {
    const pid_t cavaPid = M.cavaPid;
    const int cavaFd = M.cavaFd;
    std::string cavaBuf = std::move(M.cavaBuf);
    std::vector<double> points = std::move(M.points);
    const double position = M.position, length = M.length;  // (the ring of the bar's media keeps filling while the panel opens and closes)
    const std::string status = M.status;
    const auto polledAt = M.polledAt;
    M = MediaPanel{};
    M.position = position;
    M.length = length;
    M.status = status;
    M.polledAt = polledAt;
    M.cavaPid = cavaPid;
    M.cavaFd = cavaFd;
    M.cavaBuf = std::move(cavaBuf);
    M.points = std::move(points);
}

void closeMediaPanel(App& app) {
    MediaPanel& M = app.media;
    if (!M.open()) return;
    stopCava(app);
    if (M.layer) zwlr_layer_surface_v1_destroy(M.layer);
    hidpi::detach(M.scale);
    wl_surface_destroy(M.surface);
    if (M.cover) cairo_surface_destroy(M.cover);
    if (M.blur) cairo_surface_destroy(M.blur);
    if (M.shadow) cairo_surface_destroy(M.shadow);
    resetMediaPanel(M);
    app.onMedia = false;
    app.repeatKey = 0;
    updateCursor(app);
    malloc_trim(0);
}

void onMediaConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->media.configured) app->media.anim.animStart = std::chrono::steady_clock::now();
    app->media.configured = true;
    drawMediaPanel(*app);
}
void onMediaClosed(void* d, zwlr_layer_surface_v1*) { closeMediaPanel(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kMediaListener = {onMediaConfigure, onMediaClosed};

bool mediaLyricsSaved() {
    std::ifstream f(stateDir() + "/media-lyrics.txt");
    std::string v;
    std::getline(f, v);
    return v == "1";
}
void mediaLyricsSave(bool on) {
    std::error_code ec;
    std::filesystem::create_directories(stateDir(), ec);
    std::ofstream(stateDir() + "/media-lyrics.txt") << (on ? "1" : "0") << "\n";
}

void openMediaPanel(App& app, Bar* bar = nullptr) {
    MediaPanel& M = app.media;
    if (M.open()) return;
    closeSelector(app);
    closeLauncher(app);
    closeNotifPanel(app);
    closeClockPanel(app);
    resetMediaPanel(M);
    popStart(M.anim);
    M.lyricsMode = mediaLyricsSaved();
    M.lyricT = M.lyricsMode ? 1.0 : 0.0;
    const auto placement = barPopupAnchor(app, bar, bar ? app.cfg.mediaClickPopupPosition : app.cfg.mediaPopupPosition, mediaWidgetCentre);
    bar = placement.bar;
    const bool vert = sideBar(app), atBottom = app.cfg.barPosition == "bottom";
    const double anchor = placement.position, length = placement.length;
    const double inset = app.cfg.frame + 12;

    M.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, M.scale, M.surface, [&app] { drawMediaPanel(app); });
    M.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, M.surface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-media");
    constexpr uint32_t kB = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, kT = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, kL = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, kR = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    M.anchoredBottom = !vert && atBottom;
    const bool right = vert && app.cfg.barPosition == "right";
    zwlr_layer_surface_v1_set_anchor(M.layer, (M.anchoredBottom ? kB : kT) | (right ? kR : kL));
    if (vert) {
        M.cardX = static_cast<int>(kMediaRoom);
        const int side = std::max(0, sideCardEdge(app) - static_cast<int>(kMediaRoom));
        M.side = barPopupSide(placement, inset, kMediaRoom, kMediaSurfH);
        if (placement.followsWidget) M.along = anchor - M.side.surfaceTop;
        zwlr_layer_surface_v1_set_margin(M.layer, M.side.surfaceTop, right ? side : 0, 0, right ? 0 : side);
    } else {
        const int left = static_cast<int>(barPopupNearEdge(anchor, length, kMediaCardW, kMediaCardW, inset));
        const int surfLeft = std::max(0, left - static_cast<int>(kMediaRoom));
        M.cardX = left - surfLeft;
        if (placement.followsWidget) M.along = anchor - surfLeft;
        const int margin = std::max(0, static_cast<int>(app.cfg.frame + placement.gap + kBarHeight + 12 - kMediaRoom));
        zwlr_layer_surface_v1_set_margin(M.layer, atBottom ? 0 : margin, 0, atBottom ? margin : 0, surfLeft);
    }
    zwlr_layer_surface_v1_set_size(M.layer, kMediaSurfW, kMediaSurfH);
    zwlr_layer_surface_v1_set_exclusive_zone(M.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(M.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(M.layer, &kMediaListener, &app);
    wl_surface_commit(M.surface);
    pollMedia(app);
    if (M.status == "Playing") startCava(app);
}

void retractMediaPanel(App& app) {
    MediaPanel& M = app.media;
    if (!M.open() || popIsOut(M.anim)) return;
    if (!M.configured) { closeMediaPanel(app); return; }
    popRetract(M.anim, std::chrono::steady_clock::now());
    M.hover = 0;
    M.drag = 0;
    drawMediaPanel(app);
}

void toggleMediaPanel(App& app, Bar* bar = nullptr) {
    MediaPanel& M = app.media;
    if (!M.open()) { openMediaPanel(app, bar); return; }
    if (popIsOut(M.anim)) {
        popReverse(M.anim, std::chrono::steady_clock::now());
        drawMediaPanel(app);
    } else retractMediaPanel(app);
}

pid_t mediaSend(App& app, std::initializer_list<const char*> args) {
    if (app.musicPlayer.empty() || args.size() == 0) return 0;
    const std::string verb = *args.begin();
    const bool volume = verb == "volume", seek = verb == "position";
    const bool bridge = app.musicPlayer == "plasma-browser-integration";
    const std::string previousTimeline = app.musicPlaybackPlayer;
    if (seek && previousTimeline.empty()) return 0;
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        setsid();
        std::string sel = app.musicPlayer;
        if (!volume && bridge) {
            // Revalidate in the child without drawing or changing artwork. A
            // slow player must not block the input handler in the shell.
            const auto players = mediaSnapshots(app.musicPlayer);
            const auto* metadata = mediaplayer::find(players, app.musicPlayer, app.musicTitle);
            if (metadata) sel = mediaplayer::playback(players, *metadata)->instance;
            // Failed progress polls do not disable transport, but a native
            // offset must never be applied to a different bridge timeline.
            if (seek && (!metadata || sel != previousTimeline)) _exit(1);
        }
        _exit(mediaplayer::command(sel, args) ? 0 : 1);
    }
    return pid;
}

void mediaDialAt(App& app, double lx, double ly) {
    MediaPanel& M = app.media;
    const double dx = lx - (M.rArt.x + M.rArt.w / 2), dy = ly - (M.rArt.y + M.rArt.h / 2);
    double ang = std::atan2(dx, -dy);
    if (ang < 0) ang += 2 * M_PI;
    M.volume = std::clamp(ang / (2 * M_PI), 0.0, 1.0);
    const auto now = std::chrono::steady_clock::now();
    if (now - M.lastSend > std::chrono::milliseconds(120)) {
        M.lastSend = now;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%.2f", M.volume);
        mediaSend(app, {"volume", buf});
    }
}

void mediaSeekAt(App& app, double lx, bool final) {
    MediaPanel& M = app.media;
    const double pad = 4, eff = M.rSlider.w - 2 * pad;
    const double v = eff > 0 ? std::clamp((lx - M.rSlider.x - pad) / eff, 0.0, 1.0) : 0;
    M.position = v * M.length;
    M.polledAt = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (final || now - M.lastSend > std::chrono::milliseconds(150)) {
        M.lastSend = now;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", M.position);
        mediaSend(app, {"position", buf});
    }
}

// What of the EQ page is under the pointer: 100 + band (10: the preamp), 200 + preset, 300 the switch; -1 nothing.
int mediaEqHit(const MediaPanel& M, double lx, double ly) {
    if (!M.eqMode || M.eqT < 0.6) return -1;
    if (M.rEqToggle.contains(lx, ly)) return 300;
    for (int i = 0; i < kEqPresetCount; i++) if (M.eqChip[i].contains(lx, ly)) return 200 + i;
    for (int i = 0; i <= kEqBands; i++) if (M.eqBand[i].contains(lx, ly)) return 100 + i;
    return -1;
}

void mediaEqChanged(App& app, bool final) {
    MediaPanel& M = app.media;
    EqState& E = app.eq;
    E.enabled = true;
    E.preset = eqMatchPreset(E);
    const auto now = std::chrono::steady_clock::now();
    if (final || now - M.eqApplied > std::chrono::milliseconds(150)) {
        M.eqApplied = now;
        eqApply(E);
    }
    if (final) eqSave(E);
}

void mediaEqDragTo(App& app, double ly, bool final) {
    MediaPanel& M = app.media;
    EqState& E = app.eq;
    const int id = M.eqDrag;
    if (id < 0 || id > kEqBands) return;
    const Rect& r = M.eqBand[id];
    const double top = r.y + 10, bottom = r.y + r.h - 10, mid = (top + bottom) / 2, half = (bottom - top) / 2;
    const double v = std::clamp(std::round((mid - ly) / half * kEqRange * 2) / 2, -kEqRange, kEqRange);
    (id == kEqBands ? E.preamp : E.gain[id]) = v;
    mediaEqChanged(app, final);
}

void mediaPanelMotion(App& app) {
    MediaPanel& M = app.media;
    if (popIsOut(M.anim)) return;
    const double lx = app.pointerX - M.originX, ly = app.pointerY - M.originY;
    if (M.drag == 1) { mediaSeekAt(app, lx, false); drawMediaPanel(app); return; }
    if (M.drag == 2) { mediaDialAt(app, lx, ly); drawMediaPanel(app); return; }
    if (M.eqDrag >= 0) { mediaEqDragTo(app, ly, false); drawMediaPanel(app); return; }
    const int eqHover = mediaEqHit(M, lx, ly);
    int hover = 0;
    if (M.rPrev.contains(lx, ly)) hover = 1;
    else if (M.rPlay.contains(lx, ly)) hover = 2;
    else if (M.rNext.contains(lx, ly)) hover = 3;
    else if (M.rPin.contains(lx, ly)) hover = 6;
    else if (M.rLyrics.contains(lx, ly)) hover = 7;
    else if (M.rEq.contains(lx, ly)) hover = 8;
    else if (eqHover >= 0) hover = 0;
    else if (M.rArt.contains(lx, ly)) hover = 4;
    else if (M.rSlider.contains(lx, ly)) hover = 5;
    if (hover == M.hover && eqHover == M.eqHover) return;
    M.hover = hover;
    M.eqHover = eqHover;
    drawMediaPanel(app);
    updateCursor(app);
}

void mediaPanelClick(App& app) {
    MediaPanel& M = app.media;
    const double lx = app.pointerX - M.originX, ly = app.pointerY - M.originY;
    if (popIsOut(M.anim)) return;
    if (!Rect{0, 0, kMediaCardW, M.cardH}.contains(lx, ly)) { retractMediaPanel(app); return; }
    if (const int eqHit = mediaEqHit(M, lx, ly); eqHit >= 0) {
        EqState& E = app.eq;
        if (eqHit == 300) {
            E.enabled = !E.enabled;
            eqSave(E);
            eqApply(E);
        } else if (eqHit >= 200) {
            for (int b = 0; b < kEqBands; b++) E.gain[b] = kEqPresets[eqHit - 200].gain[b];
            mediaEqChanged(app, true);
        } else {
            M.eqDrag = eqHit - 100;
            mediaEqDragTo(app, ly, false);
        }
    } else if (M.rEq.contains(lx, ly)) {
        M.eqMode = !M.eqMode;
        M.eqLast = std::chrono::steady_clock::now();
        M.eqMoving = true;
        M.eqHover = -1;
    } else if (M.rPin.contains(lx, ly)) {
        app.mediaPinned = !app.mediaPinned;
        mediaApplyInput(app, M.originY);
    } else if (M.rLyrics.contains(lx, ly) && M.eqMode) {
        M.eqMode = false;
        M.eqLast = std::chrono::steady_clock::now();
        M.eqMoving = true;
    } else if (M.rLyrics.contains(lx, ly)) {
        M.lyricsMode = !M.lyricsMode;
        mediaLyricsSave(M.lyricsMode);
        M.lyricLast = std::chrono::steady_clock::now();
        M.lyricMoving = true;
    } else if (M.lyricsMode && M.rLyricsRetry.contains(lx, ly)) {
        lyricsSvcRequest(app.musicTitle, app.musicArtist, M.length, app.cfg.lyricsProvider);
    } else if (M.rPrev.contains(lx, ly)) { mediaSend(app, {"previous"}); }
    else if (M.rNext.contains(lx, ly)) { mediaSend(app, {"next"}); }
    else if (M.rPlay.contains(lx, ly)) {
        mediaSend(app, {"play-pause"});
        M.status = M.status == "Playing" ? "Paused" : "Playing";
        M.position = mediaPositionNow(M);
        M.polledAt = std::chrono::steady_clock::now();
        if (M.status == "Playing") startCava(app); else stopCava(app);
    } else if (M.rSlider.contains(lx, ly) && M.length > 0) { M.drag = 1; mediaSeekAt(app, lx, false); }
    else if (M.rArt.contains(lx, ly)) { M.drag = 2; mediaDialAt(app, lx, ly); }
    drawMediaPanel(app);
}

void mediaPanelRelease(App& app) {
    MediaPanel& M = app.media;
    if (M.eqDrag >= 0) {
        mediaEqDragTo(app, app.pointerY - M.originY, true);
        M.eqDrag = -1;
        drawMediaPanel(app);
        return;
    }
    if (M.drag == 1) {
        mediaSeekAt(app, app.pointerX - M.originX, true);
        M.drag = 0;
        pollMedia(app);
    } else if (M.drag == 2) {
        M.drag = 0;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%.2f", M.volume);
        mediaSend(app, {"volume", buf});
        pollMedia(app);
    }
    drawMediaPanel(app);
}

void mediaPanelScroll(App& app, int dir) {
    MediaPanel& M = app.media;
    const double lx = app.pointerX - M.originX, ly = app.pointerY - M.originY;
    if (const int eqHit = mediaEqHit(M, lx, ly); eqHit >= 100 && eqHit < 200) {
        EqState& E = app.eq;
        const int id = eqHit - 100;
        double& v = id == kEqBands ? E.preamp : E.gain[id];
        v = std::clamp(v + (dir > 0 ? -1.0 : 1.0), -kEqRange, kEqRange);
        mediaEqChanged(app, true);
        drawMediaPanel(app);
        return;
    }
    if (!M.rArt.contains(lx, ly)) return;
    M.volume = std::clamp(M.volume + (dir > 0 ? -0.05 : 0.05), 0.0, 1.0);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.2f", M.volume);
    mediaSend(app, {"volume", buf});
    drawMediaPanel(app);
}

void readCava(App& app) {
    MediaPanel& M = app.media;
    if (M.cavaFd < 0) return;
    char buf[4096];
    for (;;) {
        const ssize_t n = read(M.cavaFd, buf, sizeof buf);
        if (n > 0) { M.cavaBuf.append(buf, n); if (M.cavaBuf.size() > 65536) M.cavaBuf.clear(); continue; }
        if (n == 0) { M.cavaPid = 0; killCava(app); app.vizShown.clear(); return; }
        break;
    }
    const size_t end = M.cavaBuf.rfind('\n');
    if (end == std::string::npos) return;
    const size_t start = M.cavaBuf.rfind('\n', end == 0 ? 0 : end - 1);
    const std::string line = M.cavaBuf.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start + 1));
    M.cavaBuf.erase(0, end + 1);
    M.points.clear();
    size_t p = 0;
    while (p < line.size()) {
        size_t q = line.find(';', p);
        if (q == std::string::npos) q = line.size();
        if (q > p) M.points.push_back(std::atof(line.substr(p, q - p).c_str()));
        p = q + 1;
    }
}

void vizFrame(App& app, std::chrono::steady_clock::time_point now) {
    if (!vizWanted(app)) return;
    if (app.media.cavaPid <= 0) {
        if (!app.vizShown.empty()) { app.vizShown.clear(); for (auto& bar : app.bars) draw(*bar); }
        return;
    }
    deskVizFrame(app, now);
    if (!barVizWanted(app) || now < app.vizNext) return;
    app.vizNext = now + std::chrono::milliseconds(33);
    const auto& pts = app.media.points;
    app.vizShown.resize(kVizBars, 0.0);
    for (int i = 0; i < kVizBars; i++) {
        const double target = pts.empty() ? 0.0 : std::clamp(pts[size_t(i) * pts.size() / kVizBars] / 1000.0, 0.0, 1.0);
        app.vizShown[i] += (target - app.vizShown[i]) * 0.5;
    }
    for (auto& bar : app.bars) draw(*bar);
}

bool mediaAnimating(const App& app) {
    const MediaPanel& M = app.media;
    return M.open() && M.configured && (M.status == "Playing" || M.drag != 0 || M.lyricMoving || M.eqMoving || M.eqDrag >= 0 || (M.lyricsMode && M.lyricStatus != "ok"));
}

std::chrono::steady_clock::time_point mediaWake(const App& app) {
    const auto base = mediaAnimating(app) ? app.media.nextFrame : app.media.nextPoll;
    if (!app.media.configured) return base;
    return std::min(base, popWake(app.media.anim.phase, app.media.anim.settleAt, std::chrono::steady_clock::now()));
}

void tickMediaPanel(App& app, std::chrono::steady_clock::time_point now) {
    MediaPanel& M = app.media;
    if (!M.open()) return;
    if (M.configured) {
        bool moving = false;
        if (popTick(M.anim, now, moving)) { closeMediaPanel(app); return; }
        if (moving) { drawMediaPanel(app); M.nextFrame = now + std::chrono::milliseconds(kMediaFrameMs); }
    }
    if (now >= M.nextPoll && M.drag == 0) {
        const std::string before = M.status;
        pollMedia(app);
        if (M.status == "Playing") startCava(app); else if (before == "Playing") stopCava(app);
        if (M.configured && !mediaAnimating(app)) drawMediaPanel(app);
    }
    if (mediaAnimating(app) && now >= M.nextFrame) {
        M.nextFrame = now + std::chrono::milliseconds(kMediaFrameMs);
        drawMediaPanel(app);
    }
}

void toggleSettings() {
    const std::string lockPath = runtimeDir() + "/end4pC-settings.lock";
    const int lock = open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    const bool running = lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) != 0;
    if (lock >= 0) close(lock);
    if (running) { runDetached({"pkill", "-x", "end4pC-settings"}); return; }
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-settings";
    runDetached({path.c_str()});
}

void toggleTools() {
    const int lock = open((runtimeDir() + "/end4pC-tools.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    const bool running = lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) != 0;
    if (lock >= 0) close(lock);
    if (running) { runDetached({"pkill", "-x", "end4pC-tools"}); return; }
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-tools";
    runDetached({path.c_str()});
}

// The Task Manager is the same program as Tools in another mode: a window of its own, with its own process name.
void toggleTasks() {
    const int lock = open((runtimeDir() + "/end4pC-tasks.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    const bool running = lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) != 0;
    if (lock >= 0) close(lock);
    if (running) { runDetached({"pkill", "-x", "end4pC-tasks"}); return; }
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-tools";
    runDetached({path.c_str(), "tasks"});
}

// The music player (end4pC-play3r) is a program of its own. No path: it opens, or closes when it is open. With a path (a song or a folder): it plays
// it, in the open window or in a new one.
void togglePlay3r(const std::string& argument) {
    const int lock = open((runtimeDir() + "/end4pC-play3r.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    const bool running = lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) != 0;
    if (lock >= 0) close(lock);
    if (running) {
        if (argument.empty()) runDetached({"pkill", "-x", "end4pC-play3r"});
        else runDetached({"playerctl", "-p", "play3r", "open", ("file://" + argument).c_str()});
        return;
    }
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-play3r";
    if (argument.empty()) runDetached({path.c_str()});
    else runDetached({path.c_str(), argument.c_str()});
}

// The file manager (end4pC-fil3s): every call opens a window (a folder given is the one it shows).
void openFil3s(const std::string& argument) {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-fil3s";
    if (argument.empty()) runDetached({path.c_str()});
    else runDetached({path.c_str(), argument.c_str()});
}

// The text editor (end4pC-t3xt): every call opens a window (files given are opened in tabs).
void openT3xt(const std::string& argument) {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-t3xt";
    if (argument.empty()) runDetached({path.c_str()});
    else runDetached({path.c_str(), argument.c_str()});
}

// The image viewer (end4pC-imag3s): every call opens a window (on the picture given, if any).
void openImag3s(const std::string& argument) {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-imag3s";
    if (argument.empty()) runDetached({path.c_str()});
    else runDetached({path.c_str(), argument.c_str()});
}

// The video player (end4pC-vid3os): every call opens a window (playing the file given, if any).
void openVid3os(const std::string& argument) {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-vid3os";
    if (argument.empty()) runDetached({path.c_str()});
    else runDetached({path.c_str(), argument.c_str()});
}

void startUpdatesCheck(const Config& cfg) {
    const std::string script = assetsDir() + "/../scripts/updates.sh";
    runDetached({script.c_str(), cfg.updatesAur ? "1" : "0", cfg.updatesFlatpak ? "1" : "0"});
}
constexpr int kUpdatesCheckWaitS = 90;
constexpr int kUpdatesRecheckMin = 10;

int updatesCountFromFile(const Config& cfg) {
    const std::string t = ccFileRead(ccCacheDir() + "/updates.json");
    if (t.find("\"pacman\":") == std::string::npos) return -1;
    auto count = [&t](const char* key) {
        const size_t p = t.find(key);
        return p == std::string::npos ? 0 : std::max(0, std::atoi(t.c_str() + p + std::strlen(key)));
    };
    return count("\"pacman\":") + (cfg.updatesAur ? count("\"aur\":") : 0) + (cfg.updatesFlatpak ? count("\"flatpak\":") : 0);
}
bool refreshUpdatesCount(App& app) {
    struct stat st {};
    const bool exists = stat((ccCacheDir() + "/updates.json").c_str(), &st) == 0;
    const time_t stamp = exists ? st.st_mtime : -1;
    const int sources = (app.cfg.updatesAur ? 1 : 0) | (app.cfg.updatesFlatpak ? 2 : 0);
    if (stamp == app.updatesStamp && sources == app.updatesSources) return false;
    if (stamp != app.updatesStamp) app.updatesChecking = false;
    app.updatesStamp = stamp;
    app.updatesSources = sources;
    app.updatesTotal = exists ? updatesCountFromFile(app.cfg) : -1;
    return true;
}
void startUpdatesNow(App& app) {
    app.updatesLast = std::chrono::steady_clock::now();
    app.updatesChecking = true;
    app.updatesCheckEnd = app.updatesLast + std::chrono::seconds(kUpdatesCheckWaitS);
    startUpdatesCheck(app.cfg);
    drawAll(app);
}
void runUpdatesTerminal(App& app) {
    runSystemUpdate(app);
    app.updatesRecheckAt = std::chrono::steady_clock::now() + std::chrono::minutes(kUpdatesRecheckMin);
}

void startLock() {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string path = n > 0 ? std::string(exe, n) : "";
    path = path.substr(0, path.rfind('/') + 1) + "end4pC-lock";
    runDetached({path.c_str()});
}
