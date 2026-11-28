// Imag3s: drawing the marks over a picture, and putting picture and marks together into the one picture that is saved or copied.
// Included once by images.cpp, inside its anonymous namespace (after gui/draw.h and model.h).
#pragma once

// One mark, in the coordinates of the picture (the caller has set the matrix). `base` is the picture itself (the pixelate tool shows a coarse copy of it).
void drawMark(cairo_t* cr, const im::Mark& m, cairo_surface_t* base) {
    if (m.pts.empty()) return;
    const im::Pt a = m.pts.front(), b = m.pts.back();
    cairo_save(cr);
    cairo_set_source_rgb(cr, m.rgb[0], m.rgb[1], m.rgb[2]);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    switch (m.tool) {
        case im::Tool::Pen:
        case im::Tool::Marker: {
            const bool marker = m.tool == im::Tool::Marker;
            if (marker) { cairo_set_source_rgba(cr, m.rgb[0], m.rgb[1], m.rgb[2], 0.38); cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE); }
            cairo_set_line_width(cr, marker ? m.width * 3.5 : m.width);
            cairo_move_to(cr, a.x, a.y);
            if (m.pts.size() == 1) cairo_line_to(cr, a.x, a.y);
            for (size_t i = 1; i < m.pts.size(); i++) cairo_line_to(cr, m.pts[i].x, m.pts[i].y);
            cairo_stroke(cr);
            break;
        }
        case im::Tool::Arrow: {
            const double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
            if (len < 1) break;
            const double ux = dx / len, uy = dy / len, head = std::min(len * 0.6, std::max(m.width * 4.5, 10.0)), spread = 0.5;
            cairo_set_line_width(cr, m.width);
            cairo_move_to(cr, a.x, a.y);
            cairo_line_to(cr, b.x - ux * head * 0.8, b.y - uy * head * 0.8);
            cairo_stroke(cr);
            cairo_move_to(cr, b.x, b.y);
            cairo_line_to(cr, b.x - head * (ux * std::cos(spread) - uy * std::sin(spread)), b.y - head * (uy * std::cos(spread) + ux * std::sin(spread)));
            cairo_line_to(cr, b.x - head * (ux * std::cos(spread) + uy * std::sin(spread)), b.y - head * (uy * std::cos(spread) - ux * std::sin(spread)));
            cairo_close_path(cr);
            cairo_fill(cr);
            break;
        }
        case im::Tool::Rect: {
            cairo_set_line_width(cr, m.width);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
            cairo_rectangle(cr, std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y));
            cairo_stroke(cr);
            break;
        }
        case im::Tool::Ellipse: {
            const double rx = std::fabs(b.x - a.x) / 2, ry = std::fabs(b.y - a.y) / 2;
            if (rx < 0.5 || ry < 0.5) break;
            cairo_set_line_width(cr, m.width);
            cairo_save(cr);
            cairo_translate(cr, (a.x + b.x) / 2, (a.y + b.y) / 2);
            cairo_scale(cr, rx, ry);
            cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
            cairo_restore(cr);  // (the path is kept; the stroke is drawn with the pen of the picture, not squashed)
            cairo_stroke(cr);
            break;
        }
        case im::Tool::Pixelate: {
            if (!base) break;
            const double iw = cairo_image_surface_get_width(base), ih = cairo_image_surface_get_height(base);
            const double x0 = std::clamp(std::min(a.x, b.x), 0.0, iw), y0 = std::clamp(std::min(a.y, b.y), 0.0, ih);
            const double x1 = std::clamp(std::max(a.x, b.x), 0.0, iw), y1 = std::clamp(std::max(a.y, b.y), 0.0, ih);
            const double rw = x1 - x0, rh = y1 - y0;
            if (rw < 2 || rh < 2) break;
            const double block = std::max(8.0, std::max(iw, ih) / 70.0);
            const int bw = std::max(1, static_cast<int>(std::ceil(rw / block))), bh = std::max(1, static_cast<int>(std::ceil(rh / block)));
            cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh);
            cairo_t* sc = cairo_create(small);
            cairo_scale(sc, bw / rw, bh / rh);
            cairo_set_source_surface(sc, base, -x0, -y0);
            cairo_pattern_set_filter(cairo_get_source(sc), CAIRO_FILTER_GOOD);
            cairo_paint(sc);
            cairo_destroy(sc);
            cairo_rectangle(cr, x0, y0, rw, rh);
            cairo_clip(cr);
            cairo_translate(cr, x0, y0);
            cairo_scale(cr, rw / bw, rh / bh);
            cairo_set_source_surface(cr, small, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
            cairo_paint(cr);
            cairo_surface_destroy(small);
            break;
        }
        case im::Tool::Text: {
            PangoLayout* l = pango_cairo_create_layout(cr);
            setFont(l, m.width, true);
            pango_layout_set_text(l, m.text.c_str(), -1);
            cairo_move_to(cr, a.x, a.y);
            pango_cairo_show_layout(cr, l);
            g_object_unref(l);
            break;
        }
    }
    cairo_restore(cr);
}

// The size of the text mark being typed, and where its caret goes (after its last character).
void textExtent(const im::Mark& m, double& w, double& h, double& caretX, double& caretY, double& caretH) {
    PangoLayout* l = pango_layout_new(pango_layout_get_context(g_measure));
    setFont(l, m.width, true);
    pango_layout_set_text(l, m.text.c_str(), -1);
    int pw = 0, ph = 0;
    pango_layout_get_pixel_size(l, &pw, &ph);
    w = pw;
    h = ph;
    PangoRectangle end;
    pango_layout_index_to_pos(l, static_cast<int>(m.text.size()), &end);
    caretX = end.x / double(PANGO_SCALE);
    caretY = end.y / double(PANGO_SCALE);
    caretH = end.height / double(PANGO_SCALE);
    g_object_unref(l);
}

// The picture with its marks, turned and mirrored as it was left: what is saved and copied.
cairo_surface_t* flatten(cairo_surface_t* base, const im::Edits& e) {
    const double w = cairo_image_surface_get_width(base), h = cairo_image_surface_get_height(base);
    const im::Pt size = im::displaySize(w, h, e.rot);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(size.x), static_cast<int>(size.y));
    cairo_t* cr = cairo_create(out);
    const im::Aff o = im::orientation(w, h, e.rot, e.flip);
    const cairo_matrix_t mtx = {o.a, o.b, o.c, o.d, o.e, o.f};
    cairo_set_matrix(cr, &mtx);
    cairo_set_source_surface(cr, base, 0, 0);
    cairo_paint(cr);
    for (const im::Mark& m : e.marks) if (!(m.tool == im::Tool::Text && m.text.empty())) drawMark(cr, m, base);
    cairo_destroy(cr);
    cairo_surface_flush(out);
    return out;
}

cairo_status_t appendBytes(void* closure, const unsigned char* data, unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}
std::string pngBytes(cairo_surface_t* s) {
    std::string out;
    if (cairo_surface_write_to_png_stream(s, appendBytes, &out) != CAIRO_STATUS_SUCCESS) out.clear();
    return out;
}

// A JPEG has no transparency: the picture goes over white.
bool saveJpeg(cairo_surface_t* s, const std::string& path) {
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s), stride = cairo_image_surface_get_stride(s);
    GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
    if (!pb) return false;
    const unsigned char* src = cairo_image_surface_get_data(s);
    guchar* dst = gdk_pixbuf_get_pixels(pb);
    const int drs = gdk_pixbuf_get_rowstride(pb);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint32_t p = *reinterpret_cast<const uint32_t*>(src + y * stride + x * 4);
            const unsigned a = p >> 24;  // premultiplied: over white = colour + (255 - alpha)
            guchar* q = dst + y * drs + x * 3;
            q[0] = static_cast<guchar>(std::min(255u, ((p >> 16) & 255) + 255 - a));
            q[1] = static_cast<guchar>(std::min(255u, ((p >> 8) & 255) + 255 - a));
            q[2] = static_cast<guchar>(std::min(255u, (p & 255) + 255 - a));
        }
    GError* err = nullptr;
    const gboolean ok = gdk_pixbuf_save(pb, path.c_str(), "jpeg", &err, "quality", "93", nullptr);
    if (err) g_error_free(err);
    g_object_unref(pb);
    return ok;
}
