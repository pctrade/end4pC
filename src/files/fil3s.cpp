#include "../icontheme.h"
// Fil3s: the file manager of the shell. A window of its own (a separate program, like Play3r) that is light on purpose: one process, folders read in
// a thread, thumbnails decoded in workers, no toolkit. It looks like the rest of the shell and does the everyday things: browse, search, copy, move,
// rename, trash, convert images / video / audio with one click, make and extract archives.
//   end4pC-fil3s [folder]     without arguments it opens your home folder
#include <cairo.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../clipboard.h"
#include "../elastic.h"
#include "../textsel_pango.h"
#include "../hidpi.h"
#include "../i18n.h"
#include "../shellfont.h"
#include "cursor-shape-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace {
using Clock = std::chrono::steady_clock;

#include "../gui/draw.h"
#include "model.h"
#include "convert.h"

struct Field {
    std::string s;
    size_t cur = 0;
};

constexpr int kDefaultW = 1180, kDefaultH = 720, kMinW = 760, kMinH = 480;
constexpr double kPad = 16, kHeaderH = 56, kSideW = 248, kRadius = 28;

enum Id { None, BtnBack, BtnForward, BtnUp, BtnClose, BtnView, BtnSort, BtnSplit, BtnHidden, BtnNew, AddressBar, AddressField, BtnGo, BtnCopyPath, Crumb, SideEmpty, ScrollThumb, ScrollTrack, PlaceRow, Item, SearchField, SearchClear, MenuRow, DialogOk, DialogCancel, DialogField, Scrim, StatusCancel, DiskSpace, Empty };
struct Hit { Rect r; int id, arg, pane; };
enum class ViewMode { Icons, Details, Compact };

// One entry of the right-click menu; an entry with `children` opens a second panel beside it.
enum class Act { AddPlace, RemovePlace, ResetPlaces, OpenPlace, Open, OpenWith, OtherApplications, RunExecutable, SetView, SetSort, SortOrder, FoldersFirst, HiddenLast, CopyToPane, MoveToPane, Cut, Copy, CopyPath, CopyFolderPath, EditLocation, CopyPlacePath, CopyNames, Duplicate, NewWindow, Terminal, Paste, Rename, Trash, Delete, Restore, EmptyTrash, NewFolder, NewFile, Convert, Compress, Extract, ExtractHere, SelectAll, Refresh, Hidden, None };
struct MenuItem {
    std::string label;
    const char* icon = "";
    Act act = Act::None;
    int arg = 0;
    bool enabled = true, separator = false;
    std::vector<MenuItem> children;
    std::shared_ptr<GAppInfo> application;
    mutable std::shared_ptr<cairo_surface_t> appIcon;
    mutable int appIconSize = 0;
    std::string target;
    bool checked = false;
};
enum class Dlg { None, NewFolder, NewFile, Rename, ConfirmDelete };

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

struct Pane {
    std::string cwd;
    std::vector<std::string> back, forward, crumbPaths;
    std::vector<Entry> entries;
    std::vector<int> view;
    bool loading = false, viewDirty = true, keepScroll = false;
    std::string loadError;
    uint64_t generation = 0;
    Sort sort = Sort::Name;
    bool descending = false, foldersFirst = true, hiddenLast = false;
    ViewMode mode = ViewMode::Icons;
    Field search, address;
    size_t addressAnchor = 0;
    double addressScroll = 0, addressTextX = 0;
    std::shared_ptr<StatusProbe> statusProbe = std::make_shared<StatusProbe>();
    uint64_t statusSerial = 0;
    bool selectionDirty = true, statusPending = false, sizePending = false, sizePartial = false;
    uint64_t selectionBytes = 0;
    size_t visibleFolders = 0, selectionFolders = 0, selectionFiles = 0;
    std::string selectionName, selectionType;
    fs::space_info space{};
    Clock::time_point spaceAt{};
    std::set<std::string> selected, selectAfter;
    int cursor = -1, anchor = -1;
    Elastic band;
    double scroll = 0, scrollMax = 0;
    Rect card, content, scrollTrack;
    double thumbTop = 0, thumbH = 0;
    bool dragScroll = false;
    double dragScrollOffset = 0;
    int columns = 1, rows = 1;
    double cellW = 0, cellH = 0;
    bool lasso = false;
    double lassoX0 = 0, lassoY0 = 0, lassoX1 = 0, lassoY1 = 0;
    std::set<std::string> lassoBase;
    int lastClickIndex = -1;
    Clock::time_point lastClick{}, reloadAt{};
    int inotifyFd = -1, watch = -1;
    std::shared_ptr<Lister> lister = std::make_shared<Lister>();
    std::set<std::string> waitingThumbs;
    std::map<std::string, Clock::time_point> fadingThumbs;
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
    wp_cursor_shape_manager_v1* cursorMgr = nullptr;  // the shape of the pointer is asked of the compositor (an arrow, a hand over buttons, a text cursor over fields)
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
    bool configured = false, running = true, dirty = true;

    xkb_context* xkbCtx = nullptr;
    xkb_keymap* xkbMap = nullptr;
    xkb_state* xkbState = nullptr;
    int kbRate = 25, kbDelay = 500;
    uint32_t repeatKey = 0;
    Clock::time_point repeatAt;

    double px = 0, py = 0;
    bool inside = false;
    int hoverId = None, hoverArg = 0;
    std::vector<Hit> hits;

    Pane panes[2];
    int activePane = 0, hitPane = -1, hoverPane = -1;
    bool split = false;
    bool showHidden = false, spaceAsPercent = true;
    std::string terminal = "kitty";
    int focus = 0;
    bool caretOn = true;
    Clock::time_point caretReset = Clock::now();
    std::vector<std::string> clip;
    bool clipCut = false;
    Rect sidebar;

    // Menu and dialog.
    bool menuOpen = false;
    double menuX = 0, menuY = 0;
    std::vector<MenuItem> menu;
    int menuOffset = 0;
    int menuSub = -1;                    // the entry whose panel is open
    int subPending = -2;                 // the entry the pointer rests on while another one's panel is open (-2: none): it takes over after a moment
    Clock::time_point subPendingAt{};
    Rect menuRect, subRect;
    Dlg dialog = Dlg::None;
    Field dialogField;
    std::string dialogTarget;
    std::string toast;
    Clock::time_point toastAt{};

    std::vector<Place> places, drives;   // places: the ones the user can edit (add, remove, reorder with a long press)
    // A long press on a place picks it up: it follows the pointer and the others make room; letting go drops it there.
    int pressedPlace = -1;               // the row of the sidebar under the button (-1 none, -2 the press was cancelled)
    Clock::time_point pressAt{};
    double pressX = 0, pressY = 0;
    bool placeDrag = false;
    int dropIndex = 0;
    double placeRowsY0 = 0, placeStep = 46;
    int placeCount = 0;
    Clock::time_point drivesAt{};
    bool animating = false;
    Thumbnailer thumbs;
    Worker worker;
    std::string status;
    Clock::time_point start = Clock::now(), lastTick = Clock::now();
} g;

Pane& pane() { return g.panes[g.hitPane >= 0 ? g.hitPane : g.activePane]; }
bool horizontal() { return pane().mode == ViewMode::Compact; }

int g_wakeFd = -1;

#include "ui.h"
#include "window.h"
}  // namespace

int main(int argc, char** argv) { return run(argc, argv); }
