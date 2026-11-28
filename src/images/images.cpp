// Imag3s: the image viewer of the shell. A window of its own (a separate program, like Play3r, Fil3s and T3xt): it shows a picture, goes through the
// pictures of its folder, zooms and turns, and draws over it (pen, marker, arrows, shapes, text, pixelate) before saving a copy. No toolkit.
//   end4pC-imag3s [file]     without arguments it asks for a picture
#include <cairo.h>
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <librsvg/rsvg.h>
#include <locale.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../hidpi.h"
#include "../i18n.h"
#include "../shellfont.h"
#include "cursor-shape-v1-client-protocol.h"
#include "model.h"
#include "xdg-shell-client-protocol.h"

namespace {
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

#include "../gui/draw.h"
#include "../gui/natural.h"
#include "../gui/proc.h"
#include "render.h"

constexpr int kDefaultW = 1100, kDefaultH = 760, kMinW = 640, kMinH = 440;
constexpr double kPad = 16, kRadius = 28, kHead = 44;
constexpr double kMaxPixels = 100e6;  // a picture bigger than this is not opened (4 bytes per pixel in memory)

enum Id { None, BtnOpen, BtnPrev, BtnNext, BtnEdit, BtnRotate, BtnCopy, BtnTrash, BtnSave, BtnClose, Canvas, NavLeft, NavRight, ZoomChip, ToolBtn, ColorBtn, WidthBtn, BtnUndo, BtnRedo,
          BtnFlip, DlgSave, DlgDiscard, DlgCancel, Scrim };
struct Hit { Rect r; int id, arg; };
enum class After { Nothing, Quit, Load };

struct Buffer {
    hidpi::Frame frame;
    wl_buffer* buffer = nullptr;
    bool busy = false;
    void clear() {
        if (buffer) wl_buffer_destroy(buffer);
        hidpi::abandon(frame);
        buffer = nullptr;
        busy = false;
    }
};

// What the loading thread hands over.
struct Loaded {
    uint64_t serial = 0;
    std::string path, error;
    cairo_surface_t* surface = nullptr;
    uint64_t bytes = 0;
};

constexpr int kColors = 7;
const double kPalette[kColors][3] = {{0, 0, 0},  // (the first is the colour of the shell, set when picked)
                                     {0.90, 0.22, 0.21}, {0.99, 0.85, 0.21}, {0.26, 0.65, 0.30}, {0.12, 0.53, 0.90}, {0.08, 0.08, 0.08}, {1, 1, 1}};

struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    xdg_wm_base* wm = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fscaleMgr = nullptr;
    wp_cursor_shape_manager_v1* cursorMgr = nullptr;
    wp_cursor_shape_device_v1* cursorDev = nullptr;
    uint32_t enterSerial = 0;
    int cursorShape = -1;
    wl_surface* surface = nullptr;
    xdg_surface* xsurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    wl_callback* frameCallback = nullptr;
    hidpi::SurfaceScale scale;
    Buffer buffers[3];
    int width = kDefaultW, height = kDefaultH;
    bool configured = false, running = true, dirty = true, fullscreen = false;

    xkb_context* xkbCtx = nullptr;
    xkb_keymap* xkbMap = nullptr;
    xkb_state* xkbState = nullptr;
    xkb_compose_table* composeTable = nullptr;
    xkb_compose_state* composeState = nullptr;
    int kbRate = 25, kbDelay = 500;
    uint32_t repeatKey = 0;
    Clock::time_point repeatAt;

    double px = 0, py = 0;
    bool inside = false;
    int hoverId = None, hoverArg = 0;
    std::vector<Hit> hits;

    // The picture: its pixels, where it came from, and the pictures beside it in the folder.
    cairo_surface_t* image = nullptr;
    std::string path;
    uint64_t bytes = 0;
    std::vector<std::string> siblings;
    std::string siblingsDir;
    int index = -1;
    std::atomic<uint64_t> loadSerial{0};
    bool loading = false;
    std::mutex loadedMutex;
    std::vector<Loaded> loaded;

    // The view.
    Rect canvas;
    bool fit = true;
    double zoom = 1;
    double panX = 0, panY = 0;
    bool panning = false;
    double panFromX = 0, panFromY = 0, panStartX = 0, panStartY = 0;
    int clickCount = 0;
    Clock::time_point lastClick{};

    // The edit.
    im::Edits edits;
    bool editing = false, drawing = false, typing = false;
    im::Tool tool = im::Tool::Pen;
    int colorIdx = 1, widthStep = 2;
    im::Mark current;
    bool caretOn = true;
    Clock::time_point caretReset = Clock::now();

    // Asking before losing the edits.
    bool dialog = false;
    After after = After::Nothing;
    std::string afterPath;
    std::string toast;
    Clock::time_point toastAt{};

    std::mutex chosenMutex;
    std::vector<std::string> chosenOpen;
    std::string chosenSave;
    std::set<std::string> extensions;  // what gdk-pixbuf can read here (the loaders installed)
} g;

int g_wakeFd = -1;

#include "ui.h"
#include "window.h"
}  // namespace

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");
    return run(argc, argv);
}
