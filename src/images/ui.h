// What Imag3s draws. Included once by images.cpp, inside its anonymous namespace.
#pragma once

bool hot(const Rect& r, int id, int arg = 0) {
    g.hits.push_back({r, id, arg});
    return g.hoverId == id && g.hoverArg == arg;
}

void discButton(cairo_t* cr, const Rect& r, int id, const char* ic, bool active = false, bool danger = false, int arg = 0, bool enabled = true) {
    const bool hov = enabled && hot(r, id, arg);
    Color bg = active ? color("secondary_container") : color("surface_container_high");
    const Color fg = danger && hov ? color("on_error_container") : active ? color("on_secondary_container") : color("on_surface");
    if (danger && hov) bg = color("error_container");
    src(cr, hov ? mix(fg, bg, 0.12) : bg, enabled ? 1.0 : 0.5);
    cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
    cairo_fill(cr);
    icon(cr, ic, r.x + (r.w - 22) / 2, r.y + (r.h - 22) / 2, 22, fg, enabled ? 1.0 : 0.38);
}

// ---- the view --------------------------------------------------------------------------------------------------------------------------
im::Pt imageSize() { return g.image ? im::Pt{double(cairo_image_surface_get_width(g.image)), double(cairo_image_surface_get_height(g.image))} : im::Pt{1, 1}; }
im::Pt shownSize() { const im::Pt s = imageSize(); return im::displaySize(s.x, s.y, g.edits.rot); }
double fitZoom() {
    const im::Pt d = shownSize();
    return std::max(0.01, std::min(1.0, std::min(g.canvas.w / d.x, g.canvas.h / d.y)));
}
// Picture point -> point of the window.
im::Aff viewMatrix() {
    const im::Pt s = imageSize(), d = shownSize();
    const im::Aff o = im::orientation(s.x, s.y, g.edits.rot, g.edits.flip);
    return o.then({g.zoom, 0, 0, g.zoom, g.canvas.x + g.canvas.w / 2 - g.zoom * d.x / 2 + g.panX, g.canvas.y + g.canvas.h / 2 - g.zoom * d.y / 2 + g.panY});
}
im::Pt toImage(double x, double y) {
    im::Aff inv;
    if (!viewMatrix().invert(inv)) return {0, 0};
    return inv.apply({x, y});
}
// Keeps some of the picture inside the window when it is dragged far away.
void clampPan() {
    const im::Pt d = shownSize();
    const double keep = 80;
    const double mx = g.canvas.w / 2 + g.zoom * d.x / 2 - keep, my = g.canvas.h / 2 + g.zoom * d.y / 2 - keep;
    g.panX = std::clamp(g.panX, -std::max(0.0, mx), std::max(0.0, mx));
    g.panY = std::clamp(g.panY, -std::max(0.0, my), std::max(0.0, my));
}
// Zoom to `z`, keeping the point of the picture under (cx, cy) where it is.
void zoomAt(double z, double cx, double cy) {
    if (!g.image) return;
    z = std::clamp(z, std::max(0.02, fitZoom() * 0.5), 40.0);
    const im::Pt p = toImage(cx, cy);
    g.fit = false;
    g.zoom = z;
    g.panX = g.panY = 0;
    const im::Pt q = viewMatrix().apply(p);
    g.panX = cx - q.x;
    g.panY = cy - q.y;
    clampPan();
    g.dirty = true;
}
void zoomFit() { g.fit = true; g.panX = g.panY = 0; g.dirty = true; }

std::string zoomLabel() { return std::to_string(static_cast<int>(std::lround(g.zoom * 100))) + "%"; }

// ---- the canvas ------------------------------------------------------------------------------------------------------------------------
void caretOf(const im::Mark& m, double& x, double& y, double& h) {
    double w, th;
    textExtent(m, w, th, x, y, h);
    x += m.pts[0].x;
    y += m.pts[0].y;
}

void drawCanvas(cairo_t* cr, const Rect& r) {
    g.canvas = r;
    if (!g.fullscreen) {
        src(cr, color("surface_container_low"));
        rrect(cr, r.x, r.y, r.w, r.h, kRadius);
        cairo_fill(cr);
    }
    hot(r, Canvas);
    if (!g.image) {
        // Nothing to show yet: a big Material shape with the icon of the program, and what to do.
        const double size = 128, cx = r.x + r.w / 2, top = r.y + r.h / 2 - size / 2 - 24;
        shapePath(cr, "Cookie12Sided", cx - size / 2, top, size);
        src(cr, color("primary_container"));
        cairo_fill(cr);
        icon(cr, "gallery", cx - 32, top + size / 2 - 32, 64, color("on_primary_container"));
        const char* msg = g.loading ? i18n::tr("Loading…") : i18n::tr("Open a picture to see it here");
        text(cr, msg, r.x, top + size + 22, 18, false, color("on_surface_variant"), 1, r.w, 1);
        return;
    }
    if (g.fit) { g.zoom = fitZoom(); g.panX = g.panY = 0; }
    else clampPan();
    cairo_save(cr);
    if (!g.fullscreen) { rrect(cr, r.x, r.y, r.w, r.h, kRadius); cairo_clip(cr); }
    else { cairo_rectangle(cr, r.x, r.y, r.w, r.h); cairo_clip(cr); }
    const im::Aff v = viewMatrix();
    const cairo_matrix_t mtx = {v.a, v.b, v.c, v.d, v.e, v.f};
    cairo_set_matrix(cr, &mtx);
    const im::Pt s = imageSize();
    src(cr, color("surface_container_highest"));  // (what shows through a transparent picture)
    cairo_rectangle(cr, 0, 0, s.x, s.y);
    cairo_fill(cr);
    cairo_set_source_surface(cr, g.image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), g.zoom >= 2 ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    for (const im::Mark& m : g.edits.marks) if (!(m.tool == im::Tool::Text && m.text.empty() && !g.typing)) drawMark(cr, m, g.image);
    if (g.drawing) drawMark(cr, g.current, g.image);
    if (g.typing && !g.edits.marks.empty() && g.caretOn) {
        double cx, cy, ch;
        const im::Mark& m = g.edits.marks.back();
        caretOf(m, cx, cy, ch);
        cairo_set_source_rgb(cr, m.rgb[0], m.rgb[1], m.rgb[2]);
        cairo_rectangle(cr, cx, cy, std::max(1.5 / g.zoom, 1.0), ch);
        cairo_fill(cr);
    }
    cairo_restore(cr);

    // The arrows to the pictures beside this one, while the pointer is over the picture.
    const bool near = g.inside && (g.hoverId == Canvas || g.hoverId == NavLeft || g.hoverId == NavRight);
    if (g.siblings.size() > 1 && !g.editing && near) {
        for (int side = 0; side < 2; side++) {
            const Rect b{side == 0 ? r.x + 16 : r.x + r.w - 16 - 48, r.y + r.h / 2 - 24, 48, 48};
            const bool hov = hot(b, side == 0 ? NavLeft : NavRight);
            src(cr, color("surface_container_high"), hov ? 0.96 : 0.82);
            cairo_arc(cr, b.x + 24, b.y + 24, 24, 0, 2 * M_PI);
            cairo_fill(cr);
            icon(cr, side == 0 ? "arrow-left" : "arrow-right", b.x + 12, b.y + 12, 24, color("on_surface"));
        }
    }
    // The zoom, top left: a click goes between "fit" and 100%.
    {
        const std::string label = zoomLabel();
        const double w = textWidth(label, 14, true) + 28, h = 32;
        const Rect z{r.x + 16, r.y + 16, w, h};
        const bool hov = hot(z, ZoomChip);
        src(cr, color("surface_container_high"), hov ? 0.98 : 0.86);
        rrect(cr, z.x, z.y, z.w, z.h, 16);
        cairo_fill(cr);
        text(cr, label, z.x, z.y + 6, 14, true, color("on_surface"), 1, z.w, 1);
    }
}

// ---- the tools -------------------------------------------------------------------------------------------------------------------------
void drawToolGlyph(cairo_t* cr, im::Tool t, double cx, double cy, const Color& fg) {
    cairo_save(cr);
    src(cr, fg);
    cairo_set_line_width(cr, 2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    switch (t) {
        case im::Tool::Pen: icon(cr, "pen", cx - 11, cy - 11, 22, fg); break;
        case im::Tool::Text: icon(cr, "text", cx - 11, cy - 11, 22, fg); break;
        case im::Tool::Marker:
            src(cr, fg, 0.45);
            cairo_set_line_width(cr, 9);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
            cairo_move_to(cr, cx - 8, cy + 4);
            cairo_line_to(cr, cx + 8, cy - 4);
            cairo_stroke(cr);
            src(cr, fg);
            cairo_set_line_width(cr, 2);
            cairo_move_to(cr, cx - 9, cy + 10);
            cairo_line_to(cr, cx + 9, cy + 10);
            cairo_stroke(cr);
            break;
        case im::Tool::Arrow:
            cairo_move_to(cr, cx - 8, cy + 8);
            cairo_line_to(cr, cx + 8, cy - 8);
            cairo_move_to(cr, cx + 1, cy - 8);
            cairo_line_to(cr, cx + 8, cy - 8);
            cairo_line_to(cr, cx + 8, cy - 1);
            cairo_stroke(cr);
            break;
        case im::Tool::Rect:
            rrect(cr, cx - 8, cy - 6, 16, 12, 2.5);
            cairo_stroke(cr);
            break;
        case im::Tool::Ellipse:
            cairo_save(cr);
            cairo_translate(cr, cx, cy);
            cairo_scale(cr, 9, 6.5);
            cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
            cairo_restore(cr);
            cairo_stroke(cr);
            break;
        case im::Tool::Pixelate:
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) {
                    src(cr, fg, (i + j) % 2 ? 0.35 : 1.0);
                    cairo_rectangle(cr, cx - 9 + i * 6.2, cy - 9 + j * 6.2, 5.6, 5.6);
                    cairo_fill(cr);
                }
            break;
    }
    cairo_restore(cr);
}

constexpr double kToolBtn = 40, kToolGap = 4, kSep = 16, kSwatch = 28;
constexpr im::Tool kTools[] = {im::Tool::Pen, im::Tool::Marker, im::Tool::Arrow, im::Tool::Rect, im::Tool::Ellipse, im::Tool::Text, im::Tool::Pixelate};
constexpr double kToolsRowW = 10 * kToolBtn + 9 * kToolGap + kSep;
constexpr double kOptionsRowW = kColors * kSwatch + (kColors - 1) * 6 + kSep + 3 * kToolBtn + 2 * kToolGap;

void sepLine(cairo_t* cr, double x, double cy) {
    src(cr, color("outline_variant"), 0.7);
    cairo_rectangle(cr, x + kSep / 2 - 0.5, cy - 12, 1, 24);
    cairo_fill(cr);
}
void roundBtn(cairo_t* cr, const Rect& b, int id, int arg, bool active, bool enabled, const std::function<void(const Color&)>& glyph) {
    const bool hov = enabled && hot(b, id, arg);
    const Color fg = active ? color("on_secondary_container") : color("on_surface");
    if (active || hov) { src(cr, active ? color("secondary_container") : color("on_surface"), active ? 1.0 : 0.09); cairo_arc(cr, b.x + b.w / 2, b.y + b.h / 2, b.w / 2, 0, 2 * M_PI); cairo_fill(cr); }
    glyph(enabled ? fg : mix(fg, color("surface_container_high"), 0.4));
}

void drawToolsRow(cairo_t* cr, double x, double y) {
    const double cy = y + kToolBtn / 2;
    auto step = [&](int id, const char* ic, bool enabled) {
        const Rect b{x, y, kToolBtn, kToolBtn};
        roundBtn(cr, b, id, 0, false, enabled, [&](const Color& fg) { icon(cr, ic, b.x + 9, b.y + 9, 22, fg); });
        x += kToolBtn + kToolGap;
    };
    step(BtnUndo, "undo-left", !g.edits.marks.empty());
    step(BtnRedo, "undo-right", !g.edits.undone.empty());
    step(BtnFlip, "flip-horizontal", true);
    x += kSep - kToolGap;
    sepLine(cr, x - kSep + kToolGap, cy);
    for (const im::Tool t : kTools) {
        const Rect b{x, y, kToolBtn, kToolBtn};
        roundBtn(cr, b, ToolBtn, static_cast<int>(t), g.tool == t, true, [&](const Color& fg) { drawToolGlyph(cr, t, b.x + kToolBtn / 2, cy, fg); });
        x += kToolBtn + kToolGap;
    }
}

Color paletteColor(int i) { return i == 0 ? color("primary") : Color{kPalette[i][0], kPalette[i][1], kPalette[i][2]}; }

void drawOptionsRow(cairo_t* cr, double x, double y) {
    const double cy = y + kToolBtn / 2;
    for (int i = 0; i < kColors; i++) {
        const Rect b{x, cy - kSwatch / 2, kSwatch, kSwatch};
        const bool hov = hot({b.x - 2, b.y - 2, b.w + 4, b.h + 4}, ColorBtn, i);
        const Color c = paletteColor(i);
        if (g.colorIdx == i) { src(cr, color("primary")); cairo_arc(cr, b.x + 14, cy, 16, 0, 2 * M_PI); cairo_fill(cr); src(cr, color("surface_container_high")); cairo_arc(cr, b.x + 14, cy, 13.5, 0, 2 * M_PI); cairo_fill(cr); }
        src(cr, c);
        cairo_arc(cr, b.x + 14, cy, hov || g.colorIdx == i ? 11 : 10, 0, 2 * M_PI);
        cairo_fill_preserve(cr);
        src(cr, color("outline"), 0.55);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        x += kSwatch + 6;
    }
    x += kSep - 6;
    sepLine(cr, x - kSep + 6, cy);
    for (int s = 1; s <= 3; s++) {
        const Rect b{x, y, kToolBtn, kToolBtn};
        roundBtn(cr, b, WidthBtn, s, g.widthStep == s, true, [&](const Color& fg) {
            src(cr, fg);
            cairo_arc(cr, b.x + kToolBtn / 2, cy, 2.0 + s * 1.9, 0, 2 * M_PI);
            cairo_fill(cr);
        });
        x += kToolBtn + kToolGap;
    }
}

// The bar at the bottom while editing: on one row when there is room, on two when the window is narrow.
void drawToolbar(cairo_t* cr, const Rect& area) {
    const double padX = 14, padY = 8;
    const bool one = area.w - 32 >= kToolsRowW + 16 + kOptionsRowW + 2 * padX;
    const double w = (one ? kToolsRowW + 16 + kOptionsRowW : std::max(kToolsRowW, kOptionsRowW)) + 2 * padX;
    const double h = (one ? kToolBtn : 2 * kToolBtn + 6) + 2 * padY;
    const Rect bar{area.x + (area.w - w) / 2, area.y + area.h - 16 - h, w, h};
    src(cr, color("surface_container_high"), 0.97);
    rrect(cr, bar.x, bar.y, bar.w, bar.h, std::min(28.0, h / 2));
    cairo_fill(cr);
    hot(bar, Scrim, 1);  // (a click on the bar is not a click on the picture under it)
    if (one) {
        drawToolsRow(cr, bar.x + padX, bar.y + padY);
        drawOptionsRow(cr, bar.x + padX + kToolsRowW + 16, bar.y + padY);
    } else {
        drawToolsRow(cr, bar.x + (w - kToolsRowW) / 2, bar.y + padY);
        drawOptionsRow(cr, bar.x + (w - kOptionsRowW) / 2, bar.y + padY + kToolBtn + 6);
    }
}

// ---- the window ------------------------------------------------------------------------------------------------------------------------
std::string shortName() { return g.path.empty() ? std::string() : fs::path(g.path).filename().string(); }

void drawHeader(cairo_t* cr, int W) {
    const double y = kPad, h = kHead;
    double x = kPad;
    {  // the mark of the program: a Material shape with the icon of pictures, and its name
        shapePath(cr, "Cookie12Sided", x, y, h);
        src(cr, color("primary_container"));
        cairo_fill(cr);
        icon(cr, "gallery", x + (h - 24) / 2, y + (h - 24) / 2, 24, color("on_primary_container"));
        x += h + 10;
        const double nameW = textWidth("Imag3s", 20, true);
        text(cr, "Imag3s", x, y + (h - textHeight("Imag3s", 20, true)) / 2, 20, true, color("on_surface"));
        x += nameW + 18;
    }
    const bool several = g.siblings.size() > 1;
    double rx = W - kPad - h;
    discButton(cr, {rx, y, h, h}, BtnClose, "close-circle", false, true);
    rx -= h + 6;
    discButton(cr, {rx, y, h, h}, BtnSave, "diskette", false, false, 0, g.image && g.edits.dirty());
    rx -= h + 6;
    discButton(cr, {rx, y, h, h}, BtnTrash, "trash-bin-2", false, false, 0, !g.path.empty());
    rx -= h + 6;
    discButton(cr, {rx, y, h, h}, BtnCopy, "copy", false, false, 0, g.image != nullptr);
    rx -= h + 6;
    discButton(cr, {rx, y, h, h}, BtnRotate, "restart", false, false, 0, g.image != nullptr);
    rx -= h + 6;
    discButton(cr, {rx, y, h, h}, BtnEdit, "pen", g.editing, false, 0, g.image != nullptr);
    rx -= h + 6;
    discButton(cr, {rx, y, h, h}, BtnOpen, "folder-open");
    rx -= h + 6;
    discButton(cr, {rx, y, h, h}, BtnNext, "arrow-right", false, false, 0, several);
    rx -= h + 6;
    discButton(cr, {rx, y, h, h}, BtnPrev, "arrow-left", false, false, 0, several);
    rx -= 14;
    const double room = rx - x;
    if (room > 60 && g.image) {
        const im::Pt s = imageSize();
        gchar* size = g_format_size(g.bytes);
        std::string meta = std::to_string(static_cast<int>(s.x)) + " × " + std::to_string(static_cast<int>(s.y)) + "  ·  " + size;
        if (g.siblings.size() > 1 && g.index >= 0) meta += "  ·  " + std::to_string(g.index + 1) + " / " + std::to_string(g.siblings.size());
        g_free(size);
        const std::string name = shortName() + (g.edits.dirty() ? "  •" : "");
        text(cr, name, x, y + 3, 16, true, color("on_surface"), 1, room, 1);
        text(cr, meta, x, y + 24, 12.5, false, color("on_surface_variant"), 1, room, 1);
    }
}

void drawToast(cairo_t* cr, int W, int H) {
    if (g.toast.empty()) return;
    if (std::chrono::duration<double>(Clock::now() - g.toastAt).count() > 5) { g.toast.clear(); return; }
    const double w = std::min(W - 48.0, textWidth(g.toast, 15, false) + 48), x = (W - w) / 2, y = H - kPad - (g.editing ? 110 : 70);
    src(cr, color("inverse_surface"));
    rrect(cr, x, y, w, 48, 14);
    cairo_fill(cr);
    text(cr, g.toast, x + 24, y + 14, 15, false, color("inverse_on_surface"), 1, w - 48);
}

void drawDialog(cairo_t* cr, int W, int H) {
    if (!g.dialog) return;
    cairo_set_source_rgba(cr, 0, 0, 0, 0.32);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);
    hot({0, 0, double(W), double(H)}, Scrim);
    const double w = std::min(460.0, W - 48.0), h = 176, x = (W - w) / 2, y = (H - h) / 2 - 20;
    src(cr, color("surface_container_high"));
    rrect(cr, x, y, w, h, 28);
    cairo_fill(cr);
    hot({x, y, w, h}, Scrim, 1);
    text(cr, i18n::tr("Save a copy?"), x + 24, y + 22, 24, true, color("on_surface"));
    text(cr, i18n::tr("This picture has changes that are not saved. They are saved in a new file; the original is kept."), x + 24, y + 62, 15, false, color("on_surface_variant"), 1, w - 48, 0, true);
    const double bh = 40, by = y + h - 24 - bh;
    double rx = x + w - 24;
    for (auto [label, id, primary] : {std::tuple<const char*, int, bool>{"Save a copy", DlgSave, true}, {"Discard", DlgDiscard, false}, {"Cancel", DlgCancel, false}}) {
        const double bwid = textWidth(i18n::tr(label), 15, true) + (primary ? 48 : 32);
        const Rect b{rx - bwid, by, bwid, bh};
        rx -= bwid + 8;
        const bool hov = hot(b, id);
        if (primary) { src(cr, color("primary")); rrect(cr, b.x, b.y, b.w, b.h, 20); cairo_fill(cr); if (hov) { src(cr, color("on_primary"), 0.1); rrect(cr, b.x, b.y, b.w, b.h, 20); cairo_fill(cr); } }
        else if (hov) { src(cr, color("primary"), 0.08); rrect(cr, b.x, b.y, b.w, b.h, 20); cairo_fill(cr); }
        text(cr, i18n::tr(label), b.x, b.y + (bh - 20) / 2, 15, true, color(primary ? "on_primary" : "primary"), 1, b.w, 1);
    }
}

void drawWindow(cairo_t* cr, int W, int H) {
    g.hits.clear();
    cairo_rectangle(cr, 0, 0, W, H);
    src(cr, color(g.fullscreen ? "surface_container_lowest" : "background"));
    cairo_fill(cr);
    Rect canvas;
    if (g.fullscreen) canvas = {0, 0, double(W), double(H)};
    else { drawHeader(cr, W); canvas = {kPad, kPad + kHead + 12, W - 2 * kPad, H - kPad - (kPad + kHead + 12)}; }
    drawCanvas(cr, canvas);
    if (g.editing && g.image) drawToolbar(cr, canvas);
    drawToast(cr, W, H);
    drawDialog(cr, W, H);
}
