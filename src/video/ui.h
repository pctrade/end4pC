// What Vid3os draws: the video, and the controls over it. Included once by vid3os.cpp, inside its anonymous namespace.
#pragma once

bool hot(const Rect& r, int id) {
    g.hits.push_back({r, id});
    return g.hoverId == id;
}

// Are the controls on screen? Always when nothing plays or the video is paused; while it plays they go away after a few seconds without moving
// the mouse (and stay while the mouse is over them or something is being dragged).
bool controlsShown() {
    if (!g.loaded || g.paused || g.seeking || g.draggingVolume) return true;
    if (std::chrono::duration<double>(Clock::now() - g.lastActivity).count() < kHideAfter) return true;
    return false;
}

// A round button without a background (it gets a soft circle under the pointer), icon over the video in white, or in the colours of the shell when
// there is no video behind it.
void flatButton(cairo_t* cr, const Rect& r, int id, const char* ic, const Color& ink, bool enabled = true, double iconSize = 24) {
    const bool hov = enabled && hot(r, id);
    if (hov) { src(cr, ink, 0.16); cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI); cairo_fill(cr); }
    icon(cr, ic, r.x + (r.w - iconSize) / 2, r.y + (r.h - iconSize) / 2, static_cast<int>(iconSize), ink, enabled ? 1.0 : 0.38);
}

// A horizontal bar: a track, the part that is full, and a round handle that grows when the pointer is near.
void drawBar(cairo_t* cr, const Rect& r, double fraction, const Color& ink, bool big) {
    const double th = big ? 6 : 4, cy = r.y + r.h / 2;
    src(cr, ink, 0.30);
    rrect(cr, r.x, cy - th / 2, r.w, th, th / 2);
    cairo_fill(cr);
    const double w = std::max(0.0, std::min(1.0, fraction)) * r.w;
    src(cr, color("primary"));
    if (w > 0) { rrect(cr, r.x, cy - th / 2, std::max(w, th), th, th / 2); cairo_fill(cr); }
    src(cr, color("primary"));
    cairo_arc(cr, r.x + w, cy, big ? 8 : 6, 0, 2 * M_PI);
    cairo_fill(cr);
}

void scrim(cairo_t* cr, double x, double y, double w, double h, bool topDown) {
    cairo_pattern_t* p = cairo_pattern_create_linear(0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba(p, 0, 0, 0, 0, topDown ? 0.62 : 0.0);
    cairo_pattern_add_color_stop_rgba(p, 1, 0, 0, 0, topDown ? 0.0 : 0.70);
    cairo_set_source(cr, p);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
}

std::string titleOfFile() { return g.path.empty() ? std::string() : fs::path(g.path).filename().string(); }

void drawToast(cairo_t* cr, int W, int H) {
    if (g.toast.empty()) return;
    if (std::chrono::duration<double>(Clock::now() - g.toastAt).count() > 2.2) { g.toast.clear(); return; }
    const double w = std::min(W - 48.0, textWidth(g.toast, 15, true) + 40), x = (W - w) / 2, y = 78;
    src(cr, color("inverse_surface"), 0.94);
    rrect(cr, x, y, w, 40, 20);
    cairo_fill(cr);
    text(cr, g.toast, x, y + 10, 15, true, color("inverse_on_surface"), 1, w, 1);
}

void drawIdle(cairo_t* cr, int W, int H) {
    const double size = 128, cx = W / 2.0, top = H / 2.0 - size / 2 - 24;
    shapePath(cr, "Cookie12Sided", cx - size / 2, top, size);
    src(cr, color("primary_container"));
    cairo_fill(cr);
    icon(cr, g.loaded ? "music-notes" : "play-stream", cx - 32, top + size / 2 - 32, 64, color("on_primary_container"));
    const std::string msg = g.loaded ? titleOfFile() : i18n::tr("Open a video to play it here");
    text(cr, msg, 24, top + size + 22, 18, false, color("on_surface_variant"), 1, W - 48, 1);
}

void drawControls(cairo_t* cr, int W, int H, bool overVideo) {
    const Color ink = overVideo ? Color{1, 1, 1} : color("on_surface");
    // top: the mark of the program, the title, open and close
    if (overVideo) scrim(cr, 0, 0, W, 110, true);
    double x = kPad, y = 16;
    shapePath(cr, "Cookie12Sided", x, y, 44);
    src(cr, color("primary_container"));
    cairo_fill(cr);
    icon(cr, "play-stream", x + 10, y + 10, 24, color("on_primary_container"));
    x += 44 + 10;
    const double nameW = textWidth("Vid3os", 20, true);
    text(cr, "Vid3os", x, y + (44 - textHeight("Vid3os", 20, true)) / 2, 20, true, ink);
    x += nameW + 18;
    double rx = W - kPad - 44;
    flatButton(cr, {rx, y, 44, 44}, BtnClose, "close-circle", ink);
    rx -= 50;
    flatButton(cr, {rx, y, 44, 44}, BtnOpen, "folder-open", ink);
    rx -= 8;
    if (g.loaded && rx - x > 80) text(cr, titleOfFile(), x, y + (44 - 20) / 2, 16, true, ink, 1, rx - x, 1);

    if (!g.loaded) return;
    // bottom: the bar of time, then the buttons
    if (overVideo) scrim(cr, 0, H - 190, W, 190, false);
    const double timeW = std::max(textWidth(vid::formatTime(g.dur), 13, false), textWidth("0:00:00", 13, false)) + 4;
    const double barY = H - 120;
    text(cr, vid::formatTime(g.seeking ? g.seekFraction * g.dur : g.pos), kPad, barY + 6, 13, true, ink, 1, timeW, 0);
    text(cr, vid::formatTime(g.dur), W - kPad - timeW, barY + 6, 13, true, ink, 1, timeW, 2);
    g.seekBar = {kPad + timeW + 12, barY - 6, W - 2 * kPad - 2 * timeW - 24, 36};
    const bool seekHot = hot(g.seekBar, Seek) || g.seeking;
    const double frac = g.seeking ? g.seekFraction : (g.dur > 0 ? g.pos / g.dur : 0);
    drawBar(cr, {g.seekBar.x, g.seekBar.y + 6, g.seekBar.w, 24}, frac, ink, seekHot);

    const double rowY = H - 78;  // the top of the row of buttons (44 tall; the play button is 56)
    const double mid = W / 2.0;
    // centre: previous, play, next
    flatButton(cr, {mid - 28 - 12 - 44, rowY, 44, 44}, BtnPrev, "skip-previous", ink, g.siblings.size() > 1);
    {
        const Rect b{mid - 28, rowY - 6, 56, 56};
        const bool hov = hot(b, BtnPlay);
        src(cr, color("primary"));
        shapePath(cr, g.paused ? "Cookie9Sided" : "Circle", b.x, b.y, b.w);
        cairo_fill(cr);
        if (hov) { src(cr, color("on_primary"), 0.12); shapePath(cr, g.paused ? "Cookie9Sided" : "Circle", b.x, b.y, b.w); cairo_fill(cr); }
        icon(cr, g.paused ? "play" : "pause", b.x + 14, b.y + 14, 28, color("on_primary"));
    }
    flatButton(cr, {mid + 28 + 12, rowY, 44, 44}, BtnNext, "skip-next", ink, g.siblings.size() > 1);
    // left: volume
    flatButton(cr, {kPad, rowY, 44, 44}, BtnMute, g.muted || g.volume <= 0 ? "volume-cross" : g.volume < 40 ? "volume-small" : "volume-loud", ink);
    g.volumeBar = {kPad + 50, rowY, 96, 44};
    hot(g.volumeBar, Volume);
    drawBar(cr, {g.volumeBar.x, g.volumeBar.y + 10, g.volumeBar.w, 24}, g.muted ? 0 : g.volume / 130.0, ink, g.hoverId == Volume || g.draggingVolume);
    // right: fullscreen, audio, subtitles, speed
    rx = W - kPad - 44;
    flatButton(cr, {rx, rowY, 44, 44}, BtnFull, g.fullscreen ? "quit-full-screen" : "full-screen", ink);
    rx -= 48;
    flatButton(cr, {rx, rowY, 44, 44}, BtnAudio, "headphones-round", ink);
    if (g.audioOff) {  // no audio track: the headphones are crossed out, like something muted
        src(cr, ink);
        cairo_set_line_width(cr, 2.4);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, rx + 11, rowY + 11);
        cairo_line_to(cr, rx + 33, rowY + 33);
        cairo_stroke(cr);
    }
    rx -= 48;
    flatButton(cr, {rx, rowY, 44, 44}, BtnSubs, "subtitles", ink);
    rx -= 48;
    {  // continuous playback: a soft pill behind the icon while it is on
        const Rect b{rx, rowY, 44, 44};
        const bool hov = hot(b, BtnAuto);
        if (g.autoplay) { src(cr, color("secondary_container")); cairo_arc(cr, rx + 22, rowY + 22, 22, 0, 2 * M_PI); cairo_fill(cr); }
        else if (hov) { src(cr, ink, 0.16); cairo_arc(cr, rx + 22, rowY + 22, 22, 0, 2 * M_PI); cairo_fill(cr); }
        icon(cr, "playlist", rx + 10, rowY + 10, 24, g.autoplay ? color("on_secondary_container") : ink, g.autoplay ? 1.0 : 0.85);
    }
    rx -= 8;
    {
        const std::string label = vid::speedLabel(g.speed);
        const double w = textWidth(label, 14, true) + 28;
        const Rect b{rx - w, rowY + 4, w, 36};
        const bool hov = hot(b, BtnSpeed);
        src(cr, ink, hov ? 0.22 : 0.12);
        rrect(cr, b.x, b.y, b.w, b.h, 18);
        cairo_fill(cr);
        text(cr, label, b.x, b.y + 8, 14, true, ink, 1, b.w, 1);
    }
}

void drawWindow(cairo_t* cr, int W, int H, cairo_surface_t* video) {
    g.hits.clear();
    cairo_rectangle(cr, 0, 0, W, H);
    src(cr, g.loaded && g.hasVideo ? Color{0, 0, 0} : color("background"));
    cairo_fill(cr);
    const bool overVideo = g.loaded && g.hasVideo && video;
    hot({0, 0, double(W), double(H)}, Canvas);
    if (overVideo) {
        cairo_save(cr);
        cairo_set_source_surface(cr, video, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else drawIdle(cr, W, H);
    if (controlsShown()) drawControls(cr, W, H, overVideo);
    drawToast(cr, W, H);
}
