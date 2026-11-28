// Tests of the model of Imag3s (src/images/model.h): the turn and mirror of a picture, undo and redo of marks.
#include <cstdio>
#include <cstdlib>

#include "../../src/images/model.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    const double w = 200, h = 100;
    // every orientation maps the picture onto its turned rectangle, and back
    for (int rot = 0; rot < 4; rot++)
        for (int flip = 0; flip < 2; flip++) {
            const im::Aff m = im::orientation(w, h, rot, flip);
            const im::Pt size = im::displaySize(w, h, rot);
            for (im::Pt corner : {im::Pt{0, 0}, {w, 0}, {0, h}, {w, h}, {37, 61}}) {
                const im::Pt q = m.apply(corner);
                CHECK(q.x > -1e-9 && q.x < size.x + 1e-9 && q.y > -1e-9 && q.y < size.y + 1e-9);
                im::Aff inv;
                CHECK(m.invert(inv));
                const im::Pt back = inv.apply(q);
                CHECK(near(back.x, corner.x) && near(back.y, corner.y));
            }
        }
    // a quarter turn clockwise sends the top-left corner to the top-right
    {
        const im::Pt p = im::orientation(w, h, 1, false).apply({0, 0});
        CHECK(near(p.x, h) && near(p.y, 0));
        const im::Pt q = im::orientation(w, h, 0, true).apply({0, 10});
        CHECK(near(q.x, w) && near(q.y, 10));
    }
    // four quarter turns are the identity; mirroring twice too
    {
        im::Edits e;
        for (int i = 0; i < 4; i++) e.turn(1);
        CHECK(e.rot == 0 && !e.dirty());
        e.turn(-1);
        CHECK(e.rot == 3 && e.dirty());
        e.clear();
        e.mirror(); e.mirror();
        CHECK(!e.dirty());
    }
    // undo and redo
    {
        im::Edits e;
        im::Mark m;
        m.pts = {{1, 1}, {5, 5}};
        e.add(m); e.add(m);
        CHECK(e.marks.size() == 2 && e.dirty());
        CHECK(e.undo() && e.marks.size() == 1 && e.undone.size() == 1);
        CHECK(e.redo() && e.marks.size() == 2);
        CHECK(e.undo());
        e.add(m);  // a new mark ends the redo history
        CHECK(e.undone.empty() && !e.redo());
        e.undo(); e.undo();
        CHECK(!e.undo() && !e.dirty());
    }
    // the strokes keep their look on pictures of every size
    CHECK(im::strokeWidth(1, 100, 80) == 2.0);
    CHECK(im::strokeWidth(3, 8000, 6000) > im::strokeWidth(1, 8000, 6000) * 3.9);
    if (failures == 0) std::puts("images model: all ok");
    return failures ? 1 : 0;
}
