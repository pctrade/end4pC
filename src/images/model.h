// Imag3s: what an edit is. No window and no drawing in here (so it can be tested alone): the marks drawn over a picture, the undo and redo of them,
// and the turn / mirror of the picture, with the maths that takes a point of the picture to the screen and back.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace im {

enum class Tool { Pen, Marker, Arrow, Rect, Ellipse, Text, Pixelate };

struct Pt { double x = 0, y = 0; };

// x' = a x + c y + e,  y' = b x + d y + f   (the same order as cairo_matrix_t: xx, yx, xy, yy, x0, y0)
struct Aff {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Pt apply(Pt p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    // this first, then `o`
    Aff then(const Aff& o) const {
        return {o.a * a + o.c * b, o.b * a + o.d * b, o.a * c + o.c * d, o.b * c + o.d * d, o.a * e + o.c * f + o.e, o.b * e + o.d * f + o.f};
    }
    bool invert(Aff& out) const {
        const double det = a * d - b * c;
        if (std::fabs(det) < 1e-12) return false;
        out.a = d / det; out.b = -b / det; out.c = -c / det; out.d = a / det;
        out.e = -(out.a * e + out.c * f);
        out.f = -(out.b * e + out.d * f);
        return true;
    }
};

// The size of a w x h picture after it was turned `rot` quarter turns (clockwise).
inline Pt displaySize(double w, double h, int rot) { return (rot & 1) ? Pt{h, w} : Pt{w, h}; }

// Picture point -> point of the turned picture: first mirrored (left to right) when `flip`, then turned clockwise `rot` quarter turns.
inline Aff orientation(double w, double h, int rot, bool flip) {
    Aff m;
    if (flip) m = {-1, 0, 0, 1, w, 0};
    switch (rot & 3) {
        case 1: return m.then({0, 1, -1, 0, h, 0});
        case 2: return m.then({-1, 0, 0, -1, w, h});
        case 3: return m.then({0, -1, 1, 0, 0, w});
        default: return m;
    }
}

struct Mark {
    Tool tool = Tool::Pen;
    double rgb[3] = {1, 0, 0};
    double width = 4;   // in pixels of the picture (the size of the letters for a text)
    std::vector<Pt> pts;
    std::string text;
};

struct Edits {
    std::vector<Mark> marks, undone;
    int rot = 0;
    bool flip = false;

    void add(Mark m) { marks.push_back(std::move(m)); undone.clear(); }
    bool undo() { if (marks.empty()) return false; undone.push_back(std::move(marks.back())); marks.pop_back(); return true; }
    bool redo() { if (undone.empty()) return false; marks.push_back(std::move(undone.back())); undone.pop_back(); return true; }
    void turn(int quarters) { rot = ((rot + quarters) % 4 + 4) % 4; }
    void mirror() { flip = !flip; rot = (4 - rot) % 4; }  // (mirroring a turned picture: the turn goes the other way round)
    bool dirty() const { return !marks.empty() || rot != 0 || flip; }
    void clear() { marks.clear(); undone.clear(); rot = 0; flip = false; }
};

// How thick a stroke of `step` (1 thin, 2 medium, 3 thick) is on a picture of this size: the same look on a thumbnail and on a 40 MP photo.
inline double strokeWidth(int step, double w, double h) {
    const double unit = std::max(2.0, std::max(w, h) / 400.0);
    return unit * (step <= 1 ? 1.0 : step == 2 ? 2.0 : 4.0);
}

}  // namespace im
