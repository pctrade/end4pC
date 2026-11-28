// Vid3os: the video player of the shell. A window of its own (a separate program, like Play3r, Fil3s, T3xt and Imag3s): libmpv decodes and draws the video
// (in software, into the same shared-memory buffers as every window of the shell), and the controls are drawn over it and go away by themselves while it
// plays. It only plays: no editing. No toolkit.
//   end4pC-vid3os [file]     without arguments it waits for a file to be opened
#include <cairo.h>
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <librsvg/rsvg.h>
#include <locale.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
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
#include "util.h"
#include "xdg-shell-client-protocol.h"

namespace {
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

#include "../gui/draw.h"
#include "../gui/natural.h"
#include "../gui/proc.h"

constexpr int kDefaultW = 1100, kDefaultH = 660, kMinW = 640, kMinH = 400;
constexpr double kPad = 20;
constexpr double kHideAfter = 2.8;  // seconds without the mouse moving before the controls go away (while it plays)

enum Id { None, BtnOpen, BtnClose, BtnPlay, BtnPrev, BtnNext, BtnMute, Volume, BtnSpeed, BtnAuto, BtnSubs, BtnAudio, BtnFull, Seek, Canvas };
struct Hit { Rect r; int id; };

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
    bool cursorHidden = false;
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
    int kbRate = 25, kbDelay = 500;
    uint32_t repeatKey = 0;
    Clock::time_point repeatAt;

    double px = 0, py = 0;
    bool inside = false;
    int hoverId = None;
    std::vector<Hit> hits;

    // mpv.
    mpv_handle* mpv = nullptr;
    mpv_render_context* rctx = nullptr;
    cairo_surface_t* videoSurf = nullptr;  // what mpv drew (device pixels of the window)
    bool frameReady = false;

    // What is playing.
    std::string path;
    bool loaded = false, hasVideo = false, paused = true, ended = false;
    double pos = 0, dur = 0, volume = 100, speed = 1;
    bool muted = false, autoplay = true, audioOff = false;  // autoplay: when a video ends the next one of the folder starts
    std::vector<std::string> siblings;
    std::string siblingsDir;
    int index = -1;

    // The controls.
    Clock::time_point lastActivity = Clock::now();
    bool seeking = false, draggingVolume = false;
    double seekFraction = 0;
    Clock::time_point lastSeek{};
    Rect seekBar, volumeBar;
    int clickCount = 0;
    Clock::time_point lastClick{};
    std::string toast;
    Clock::time_point toastAt{};
    std::string pendingTrack, pendingTrackLabel;  // a track was changed: what it is called is said a moment later
    Clock::time_point pendingTrackAt{};

    std::mutex chosenMutex;
    std::vector<std::string> chosenOpen;
} g;

int g_wakeFd = -1;

#include "ui.h"
#include "window.h"
}  // namespace

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");
    return run(argc, argv);
}
