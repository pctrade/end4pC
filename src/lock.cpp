#include <cairo.h>
#include "i18n.h"
#include "identity.h"
#include "oled.h"
#include "shapes.h"
#include "shellfont.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <dirent.h>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ext-session-lock-v1-client-protocol.h"
#include "hidpi.h"

namespace {
struct Color { double r = 0, g = 0, b = 0; };

// END4PC_LOCK_TIMING=1: says on stderr how long after main() began each step of coming up was reached, to see where the time of a lock goes.
std::chrono::steady_clock::time_point g_t0;
bool g_timing = false;
void mark(const char* what) {
    if (!g_timing) return;
    std::fprintf(stderr, "end4pC-lock: %8.1f ms  %s\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_t0).count(), what);
}
// What came before main(): the exec and the dynamic linking (from the start time of the process in /proc, which counts in ticks of 10 ms).
void markBeforeMain() {
    if (!g_timing) return;
    std::ifstream f("/proc/self/stat");
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t p = s.rfind(')');
    if (p == std::string::npos) return;
    for (int i = 0; i < 20 && p != std::string::npos; i++) p = s.find(' ', p + 1);  // (the start time is the 22nd field, the 20th after the name)
    timespec ts{};
    const long hz = sysconf(_SC_CLK_TCK);
    if (p == std::string::npos || hz <= 0 || clock_gettime(CLOCK_BOOTTIME, &ts) != 0) return;
    const double began = std::strtod(s.c_str() + p + 1, nullptr) * 1000.0 / hz;
    std::fprintf(stderr, "end4pC-lock: %8.0f ms  before main (exec and dynamic linking, to the nearest %ld ms)\n", ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6 - began, 1000 / hz);
}

std::string homeDir() { const char* h = std::getenv("HOME"); return h ? h : ""; }
std::string stateDir() {
    const char* d = std::getenv("XDG_STATE_HOME");
    return (d ? std::string(d) : homeDir() + "/.local/state") + "/end4pC";
}
std::string cacheDir() {
    const char* d = std::getenv("XDG_CACHE_HOME");
    return (d ? std::string(d) : homeDir() + "/.cache") + "/end4pC";
}
std::string projectDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        std::string p(buf, n);
        p = p.substr(0, p.rfind('/'));
        return p.substr(0, p.rfind('/'));
    }
    return homeDir() + "/.config/end4pC";
}
std::string assetsDir() { return projectDir() + "/assets"; }
std::string configFile() {
    const char* d = std::getenv("XDG_DATA_HOME");
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const std::string data = d && *d ? std::string(d) : homeDir() + "/.local/share";
    const std::string home = x && *x ? std::string(x) : homeDir() + "/.config";
    const std::string now = data + "/end4pC/config.json";
    if (access(now.c_str(), F_OK) == 0) return now;
    for (const std::string& old : {home + "/end4pC-user/config.json", home + "/end4pC/config.json"})
        if (access(old.c_str(), F_OK) == 0) return old;
    return now;
}
std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool jsonValue(const std::string& text, const std::string& key, std::string& out) {
    const size_t k = text.find("\"" + key + "\"");
    if (k == std::string::npos) return false;
    const size_t colon = text.find(':', k);
    if (colon == std::string::npos) return false;
    const size_t i = text.find_first_not_of(" \t\r\n", colon + 1);
    if (i == std::string::npos) return false;
    if (text[i] == '"') {
        const size_t e = text.find('"', i + 1);
        if (e == std::string::npos) return false;
        out = text.substr(i + 1, e - i - 1);
        return true;
    }
    size_t e = i;
    while (e < text.size() && text[e] != ',' && text[e] != '}' && text[e] != '\n') e++;
    out = text.substr(i, e - i);
    return !out.empty();
}

cairo_t* mkcr(cairo_surface_t* s) {
    static cairo_font_options_t* fo = nullptr;
    if (!fo) {
        fo = cairo_font_options_create();
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
        cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
    }
    cairo_t* c = cairo_create(s);
    cairo_set_font_options(c, fo);
    return c;
}
void src(cairo_t* cr, const Color& c, double a = 1) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }
void rrect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

std::map<std::string, Color> g_colors;
void loadColors() {
    std::ifstream f(stateDir() + "/generated/colors.json");
    std::string line;
    while (std::getline(f, line)) {
        const size_t q1 = line.find('"');
        const size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        const size_t h = line.find('#', q2 == std::string::npos ? 0 : q2);
        if (q2 == std::string::npos || h == std::string::npos || line.size() < h + 7) continue;
        const unsigned v = static_cast<unsigned>(std::strtoul(line.substr(h + 1, 6).c_str(), nullptr, 16));
        g_colors[line.substr(q1 + 1, q2 - q1 - 1)] = {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
    }
    std::ifstream cf(configFile());
    std::stringstream cs;
    cs << cf.rdbuf();
    const std::string ct = cs.str();
    const size_t k = ct.find("\"uiBackground\"");
    const size_t cm = ct.find("\"colorMode\"");
    const bool light = cm != std::string::npos && ct.compare(ct.find('"', ct.find(':', cm)), 7, "\"light\"") == 0;
    if (k != std::string::npos && light && ct.compare(ct.find('"', ct.find(':', k)), 7, "\"white\"") == 0)
        for (const char* n : {"background", "surface", "surface_container_lowest"}) g_colors[n] = {1, 1, 1};
    if (k != std::string::npos && !light && ct.compare(ct.find('"', ct.find(':', k)), 6, "\"oled\"") == 0) {
        for (const char* n : {"background", "surface", "surface_dim", "surface_container_lowest"}) g_colors[n] = {0, 0, 0};
        for (const char* n : {"surface_container_low", "surface_container", "surface_container_high", "surface_container_highest", "surface_variant", "surface_bright"}) {
            auto it = g_colors.find(n);
            if (it != g_colors.end()) it->second = {it->second.r * kOledSurfaceLift, it->second.g * kOledSurfaceLift, it->second.b * kOledSurfaceLift};
        }
    }
}
Color color(const std::string& n) {
    auto it = g_colors.find(n);
    if (it != g_colors.end()) return it->second;
    return n.rfind("on_", 0) == 0 ? Color{0.9, 0.9, 0.9} : Color{0.12, 0.12, 0.12};
}

struct TextSize { int w = 0, h = 0; double baseline = 0; };
TextSize text(cairo_t* cr, const std::string& s, double x, double y, double px, bool bold, const Color& c, double alpha = 1, bool draw = true) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    PangoFontDescription* d = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, s.c_str(), -1);
    TextSize ts;
    pango_layout_get_pixel_size(l, &ts.w, &ts.h);
    ts.baseline = pango_layout_get_baseline(l) / double(PANGO_SCALE);
    if (draw) {
        src(cr, c, alpha);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, l);
    }
    g_object_unref(l);
    return ts;
}

cairo_surface_t* iconMask(const std::string& name, int size, bool solid = false) {
    static std::map<std::string, cairo_surface_t*> cache;
    const std::string key = name + "/" + std::to_string(size) + (solid ? "s" : "");
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    cairo_surface_t* mask = nullptr;
    // (two arrays that last: the lists written inside the `for` were gone before its first turn, unless the compiler follows C++23 there)
    static const char* const kSolidFirst[] = {"bold", "outline", "broken"};
    static const char* const kBrokenFirst[] = {"broken", "outline", "bold"};
    for (const char* style : solid ? kSolidFirst : kBrokenFirst) {
        const std::string path = assetsDir() + "/icons/" + style + "/" + name + ".svg";
        if (access(path.c_str(), R_OK) != 0) continue;
        RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), nullptr);
        if (!h) continue;
        mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = mkcr(mask);
        RsvgRectangle vp = {0, 0, double(size), double(size)};
        rsvg_handle_render_document(h, cr, &vp, nullptr);
        cairo_destroy(cr);
        g_object_unref(h);
        break;
    }
    cache[key] = mask;
    return mask;
}
// Masks an icon cached at `px` real pixels into a size x size logical box (px > size on a fractionally scaled window, hidpi.h).
void maskIconBox(cairo_t* cr, cairo_surface_t* m, double x, double y, int size, int px) {
    if (px == size) { cairo_mask_surface(cr, m, x, y); return; }
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, double(size) / px, double(size) / px);
    cairo_mask_surface(cr, m, 0, 0);
    cairo_restore(cr);
}
void icon(cairo_t* cr, const std::string& name, double x, double y, int size, const Color& c, bool solid = false) {
    const int px = hidpi::bufferExtent(size, hidpi::scaleOf(cr));
    if (cairo_surface_t* m = iconMask(name, px, solid)) {
        src(cr, c);
        maskIconBox(cr, m, x, y, size, px);
    }
}

struct Profile {
    std::string name, user;
    cairo_surface_t* avatar = nullptr;
} g_profile;
constexpr int kAvatar = 96;

cairo_surface_t* loadCover(const std::string& path, int w, int h) {
    int iw = 0, ih = 0;
    if (!gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) return nullptr;
    const double scale = std::max(double(w) / iw, double(h) / ih);
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::max(w, int(iw * scale + 0.5)), std::max(h, int(ih * scale + 0.5)), FALSE, nullptr);
    if (!pb) return nullptr;
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    unsigned char* dst = cairo_image_surface_get_data(img);
    const int drs = cairo_image_surface_get_stride(img);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    for (int y = 0; y < ph; y++) {
        const guchar* sp = px + y * rs;
        uint32_t* dp = reinterpret_cast<uint32_t*>(dst + y * drs);
        for (int x = 0; x < pw; x++, sp += nch) {
            const uint32_t a = nch == 4 ? sp[3] : 255;
            dp[x] = (a << 24) | ((sp[0] * a / 255) << 16) | ((sp[1] * a / 255) << 8) | (sp[2] * a / 255);
        }
    }
    cairo_surface_mark_dirty(img);
    g_object_unref(pb);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = mkcr(out);
    cairo_set_source_surface(cr, img, (w - pw) / 2.0, (h - ph) / 2.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(img);
    return out;
}

// ---- the pictures, kept decoded -----------------------------------------------------------------------------------------------------------
// Reading the wallpaper (and the avatar) from its PNG or JPEG is most of what a lock waits for before its first frame. So what loadCover() gives is
// written once to the cache folder, as the very pixels cairo paints, and the next lock reads them back: no decoding, no scaling. The file says which
// picture it came from (path, time, size), so another wallpaper, or the same file changed, is decoded again. Nothing stays in memory between locks.
// END4PC_LOCK_NOCACHE=1 turns it off (to compare).
struct PixHeader {
    char magic[8];
    uint32_t version, w, h, stride;
    uint64_t key;
};
constexpr char kPixMagic[8] = {'e', '4', 'l', 'o', 'c', 'k', 'p', 'x'};
constexpr uint32_t kPixVersion = 1;
constexpr const char* kBgPrefix = "lock-bg-";
constexpr const char* kPixSuffix = ".bgra";
bool g_pixCache = true;
std::atomic<int> g_pixWriting{0};  // the files being written now

// Which picture, at which size: 0 when the file is not there.
uint64_t sourceKey(const std::string& path, int w, int h) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return 0;
    uint64_t k = 1469598103934665603ull;  // (FNV-1a)
    const auto mix = [&k](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; i++) k = (k ^ b[i]) * 1099511628211ull;
    };
    const int64_t facts[] = {int64_t(st.st_mtim.tv_sec), int64_t(st.st_mtim.tv_nsec), int64_t(st.st_size), int64_t(st.st_ino), w, h};
    mix(path.data(), path.size());
    mix(facts, sizeof facts);
    return k ? k : 1;
}

cairo_surface_t* readPixels(const std::string& file, uint64_t key, int w, int h) {
    const int fd = open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return nullptr;
    cairo_surface_t* s = nullptr;
    PixHeader hd{};
    struct stat st {};
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    const size_t total = static_cast<size_t>(stride) * h;
    // (a file that is not whole, or of another picture or size, is not used: the size is checked before a byte of it is read into the picture)
    if (read(fd, &hd, sizeof hd) == static_cast<ssize_t>(sizeof hd) && std::memcmp(hd.magic, kPixMagic, sizeof kPixMagic) == 0 && hd.version == kPixVersion &&
        hd.w == static_cast<uint32_t>(w) && hd.h == static_cast<uint32_t>(h) && hd.stride == static_cast<uint32_t>(stride) && hd.key == key &&
        fstat(fd, &st) == 0 && static_cast<uint64_t>(st.st_size) == sizeof hd + total) {
        s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        size_t have = 0;
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS && cairo_image_surface_get_stride(s) == stride) {
            cairo_surface_flush(s);
            unsigned char* dst = cairo_image_surface_get_data(s);
            while (have < total) {
                const ssize_t n = read(fd, dst + have, total - have);
                if (n > 0) have += static_cast<size_t>(n);
                else if (n == 0 || errno != EINTR) break;
            }
            cairo_surface_mark_dirty(s);
        }
        if (have != total) { cairo_surface_destroy(s); s = nullptr; }
        else futimens(fd, nullptr);  // (used today: the ones no lock has read for a long time are removed, pruneBackgrounds)
    }
    close(fd);
    return s;
}

// The backgrounds of screen sizes that are no longer used (a monitor that is gone) do not stay for ever.
void pruneBackgrounds() {
    const std::string dir = cacheDir();
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    const time_t old = time(nullptr) - 60 * 24 * 3600;
    while (const dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.rfind(kBgPrefix, 0) != 0 || n.size() < std::strlen(kPixSuffix) || n.compare(n.size() - std::strlen(kPixSuffix), std::string::npos, kPixSuffix) != 0) continue;
        struct stat st {};
        const std::string path = dir + "/" + n;
        if (lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_mtime < old) unlink(path.c_str());
    }
    closedir(d);
}

// Written beside and renamed over, so a lock never reads half a file. The surface is one that lives as long as the program (it is only read here).
void writePixels(const std::string& file, uint64_t key, cairo_surface_t* s) {
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s), stride = cairo_image_surface_get_stride(s);
    const unsigned char* px = cairo_image_surface_get_data(s);
    if (!px || w <= 0 || h <= 0) return;
    mkdir(cacheDir().c_str(), 0755);
    const std::string tmp = file + ".tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    PixHeader hd{};
    std::memcpy(hd.magic, kPixMagic, sizeof kPixMagic);
    hd.version = kPixVersion;
    hd.w = static_cast<uint32_t>(w);
    hd.h = static_cast<uint32_t>(h);
    hd.stride = static_cast<uint32_t>(stride);
    hd.key = key;
    const auto all = [fd](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        while (n > 0) {
            const ssize_t k = write(fd, b, n);
            if (k > 0) { b += k; n -= static_cast<size_t>(k); }
            else if (k < 0 && errno == EINTR) continue;
            else return false;
        }
        return true;
    };
    const bool ok = all(&hd, sizeof hd) && all(px, static_cast<size_t>(stride) * h);
    if (close(fd) != 0 || !ok || rename(tmp.c_str(), file.c_str()) != 0) unlink(tmp.c_str());
}

// loadCover(), from the file of the last time when it is the same picture; decoded, and kept for the next time, when it is not.
cairo_surface_t* cachedCover(const std::string& path, int w, int h, const std::string& file, bool* decoded = nullptr) {
    const uint64_t key = g_pixCache ? sourceKey(path, w, h) : 0;
    if (decoded) *decoded = true;
    if (key)
        if (cairo_surface_t* s = readPixels(file, key, w, h)) {
            if (decoded) *decoded = false;
            return s;
        }
    cairo_surface_t* s = loadCover(path, w, h);
    if (s && key) {
        cairo_surface_flush(s);
        // (beside the first frame, which does not wait for it; a thread that cannot be made only means the picture is decoded again the next time)
        g_pixWriting++;
        try {
            std::thread([file, key, s]() { writePixels(file, key, s); pruneBackgrounds(); g_pixWriting--; }).detach();
        } catch (...) { g_pixWriting--; }
    }
    return s;
}
// The files are whole before the program ends (the modes that paint and leave; a lock lasts far longer than the writing).
void joinPixWriters() {
    while (g_pixWriting > 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void loadProfile() {
    const std::string ii = readFile(homeDir() + "/.config/illogical-impulse/config.json");
    std::string pic, own, display;
    jsonValue(readFile(configFile()), "avatarPicture", own);
    jsonValue(ii, "avatarPicture", pic);
    jsonValue(readFile(configFile()), "displayName", display);  // (the display name of Settings > Profile)
    const passwd* pw = getpwuid(getuid());
    g_profile.user = pw ? pw->pw_name : "";
    g_profile.name = identity::displayName(display, g_profile.user);
    for (const std::string& c : {own, pic, homeDir() + "/.face", homeDir() + "/.face.icon"})
        if (!c.empty() && access(c.c_str(), R_OK) == 0) { g_profile.avatar = cachedCover(c, kAvatar, kAvatar, cacheDir() + "/lock-avatar" + kPixSuffix); break; }
}

struct Weather {
    bool ok = false;
    double temp = 0;
    int code = 0;
    bool day = true;
    bool fahrenheit = false;
    std::string city;
};

std::mutex g_weatherMx;
Weather g_weather;
int g_wake[2] = {-1, -1};

std::string curl(const std::string& url) {
    const std::string cmd = "curl -s --max-time 8 '" + url + "' 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    pclose(f);
    return out;
}

bool weatherUnitF() {
    std::string u;
    jsonValue(readFile(configFile()), "weatherUnit", u);
    return u == "fahrenheit";
}

std::string weatherCachePath() { return cacheDir() + "/weather.json"; }

void loadWeatherCache() {
    const std::string t = readFile(weatherCachePath());
    std::string v;
    Weather w;
    if (!jsonValue(t, "temp", v)) return;
    const bool wantF = weatherUnitF();
    std::string cu;
    if (!jsonValue(t, "unit", cu) || (cu == "fahrenheit") != wantF) return;
    w.temp = std::atof(v.c_str());
    if (jsonValue(t, "code", v)) w.code = std::atoi(v.c_str());
    if (jsonValue(t, "day", v)) w.day = std::atoi(v.c_str()) != 0;
    jsonValue(t, "city", w.city);
    w.ok = true;
    w.fahrenheit = wantF;
    std::lock_guard<std::mutex> lk(g_weatherMx);
    g_weather = w;
}

void fetchWeather() {
    std::string lat, lon, city;
    std::string cityCfg;
    jsonValue(readFile(configFile()), "weatherCity", cityCfg);
    if (!cityCfg.empty()) {
        std::string q;
        for (char c : cityCfg) q += c == ' ' ? std::string("%20") : std::string(1, c);
        const std::string j = curl("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" + q);
        if (!jsonValue(j, "latitude", lat) || !jsonValue(j, "longitude", lon)) return;
        jsonValue(j, "name", city);
    } else {
        const std::string j = curl("https://ipwho.is/");
        if (j.find("\"success\": true") == std::string::npos && j.find("\"success\":true") == std::string::npos) return;
        if (!jsonValue(j, "latitude", lat) || !jsonValue(j, "longitude", lon)) return;
        jsonValue(j, "city", city);
    }
    const std::string j = curl("https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&current=temperature_2m,weather_code,is_day&timezone=auto" + std::string(weatherUnitF() ? "&temperature_unit=fahrenheit" : ""));
    const size_t cur = j.find("\"current\":{");
    if (cur == std::string::npos) return;
    const std::string c = j.substr(cur);
    std::string t, code, day;
    if (!jsonValue(c, "temperature_2m", t) || !jsonValue(c, "weather_code", code) || !jsonValue(c, "is_day", day)) return;
    Weather w;
    w.ok = true;
    w.temp = std::atof(t.c_str());
    w.code = std::atoi(code.c_str());
    w.day = std::atoi(day.c_str()) != 0;
    w.city = city;
    w.fahrenheit = weatherUnitF();
    mkdir(cacheDir().c_str(), 0755);
    if (FILE* f = std::fopen(weatherCachePath().c_str(), "w")) {
        std::fprintf(f, "{\"city\": \"%s\", \"unit\": \"%s\", \"temp\": %.1f, \"code\": %d, \"day\": %d}\n", city.c_str(), w.fahrenheit ? "fahrenheit" : "celsius", w.temp, w.code, w.day ? 1 : 0);
        std::fclose(f);
    }
    {
        std::lock_guard<std::mutex> lk(g_weatherMx);
        g_weather = w;
    }
    if (g_wake[1] >= 0) { const char b = 'w'; (void)!write(g_wake[1], &b, 1); }
}

int weatherIntervalMin() {
    std::string v;
    int m = 10;
    if (jsonValue(readFile(configFile()), "weatherInterval", v)) m = std::atoi(v.c_str());
    return std::clamp(m, 5, 120);
}

void weatherLoop() {
    for (;;) {
        struct stat st {};
        const bool fresh = stat(weatherCachePath().c_str(), &st) == 0 && time(nullptr) - st.st_mtime < weatherIntervalMin() * 60;
        if (!fresh) fetchWeather();
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void describe(const Weather& w, std::string& label, std::string& ic) {
    const int c = w.code;
    if (c == 0) { label = "Clear sky"; ic = w.day ? "sun-2" : "moon"; }
    else if (c == 1 || c == 2) { label = c == 1 ? "Mostly clear" : "Partly cloudy"; ic = w.day ? "cloud-sun" : "cloudy-moon"; }
    else if (c == 3) { label = "Overcast"; ic = "cloud"; }
    else if (c == 45 || c == 48) { label = "Fog"; ic = "fog"; }
    else if (c >= 51 && c <= 57) { label = "Drizzle"; ic = "cloud-rain"; }
    else if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) { label = "Rain"; ic = "cloud-rain"; }
    else if ((c >= 71 && c <= 77) || c == 85 || c == 86) { label = "Snow"; ic = "cloud-snowfall"; }
    else if (c >= 95) { label = "Thunderstorm"; ic = "cloud-bolt"; }
    else { label = "Cloudy"; ic = "cloud"; }
}

int pamConv(int n, const pam_message** msg, pam_response** resp, void* data) {
    auto* pw = static_cast<const std::string*>(data);
    auto* r = static_cast<pam_response*>(calloc(n, sizeof(pam_response)));
    if (!r) return PAM_BUF_ERR;
    for (int i = 0; i < n; i++)
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON) r[i].resp = strdup(pw->c_str());
    *resp = r;
    return PAM_SUCCESS;
}

void unlockKeyring(const std::string& password) {
    std::string cfg;
    jsonValue(readFile(configFile()), "unlockKeyring", cfg);
    if (cfg == "0") return;
    std::string state;
    if (FILE* f = popen("busctl --user get-property org.freedesktop.secrets /org/freedesktop/secrets/collection/login org.freedesktop.Secret.Collection Locked 2>/dev/null", "r")) {
        char buf[64] = {};
        const size_t n = fread(buf, 1, sizeof buf - 1, f);
        pclose(f);
        state.assign(buf, n);
    }
    if (state.rfind("b false", 0) == 0) return;
    if (FILE* p = popen("killall -q -u \"$(id -un)\" gnome-keyring-daemon; exec gnome-keyring-daemon --daemonize --login >/dev/null 2>&1", "w")) {
        (void)!fwrite(password.data(), 1, password.size(), p);
        pclose(p);
    }
}

bool pamCheck(const std::string& user, std::string password) {
    const char* service = access("/etc/pam.d/end4pC-lock", R_OK) == 0 ? "end4pC-lock" : access("/etc/pam.d/hyprlock", R_OK) == 0 ? "hyprlock" : "login";
    pam_conv conv = {pamConv, &password};
    pam_handle_t* h = nullptr;
    bool ok = false;
    if (pam_start(service, user.c_str(), &conv, &h) == PAM_SUCCESS) {
        ok = pam_authenticate(h, 0) == PAM_SUCCESS;
        pam_end(h, PAM_SUCCESS);
        if (ok) unlockKeyring(password);
    }
    explicit_bzero(password.data(), password.size());
    return ok;
}

// ---- fingerprint ------------------------------------------------------------------------------------------------------------------------
// Like the password, through PAM, but with its own stack (`auth sufficient pam_fprintd.so`) so the finger is waited for while the password can
// still be typed. It is used only when fprintd lists an enrolled finger for the user (`fprintd-list`): without fprintd or without a finger enrolled nothing changes.
// The stack is a file in XDG_RUNTIME_DIR read with pam_start_confdir, so nothing is needed in /etc/pam.d.
int fpConv(int n, const pam_message**, pam_response** resp, void*) {
    *resp = static_cast<pam_response*>(calloc(n, sizeof(pam_response)));  // (the messages only say "place your finger"; there is nothing to answer)
    return *resp ? PAM_SUCCESS : PAM_BUF_ERR;
}

constexpr const char* kFpService = "end4pc-fprintd";

std::string fpConfDir() {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    if (!rt || !*rt) return "";
    const std::string dir = std::string(rt) + "/end4pC-lock-pam";
    ::mkdir(dir.c_str(), 0700);
    // (the name of the file is all lowercase: libpam lowercases the service name before it looks for the file, so a name with capitals is never found
    // and pam_start fails with "no default config other")
    std::ofstream o(dir + "/" + kFpService, std::ios::trunc);
    o << "auth sufficient pam_fprintd.so\nauth required pam_deny.so\n";
    return o ? dir : "";
}

// Where the module of fprintd is, if it is installed (the distros keep PAM modules in different folders).
std::string fpModule() {
    for (const char* d : {"/usr/lib/security", "/lib/security", "/usr/lib64/security", "/lib64/security", "/usr/lib/x86_64-linux-gnu/security", "/lib/x86_64-linux-gnu/security",
                          "/usr/lib/aarch64-linux-gnu/security", "/lib/aarch64-linux-gnu/security"}) {
        const std::string path = std::string(d) + "/pam_fprintd.so";
        if (access(path.c_str(), R_OK) == 0) return path;
    }
    return "";
}

bool fpEnrolled(const std::string& user) {
    std::string quoted = "'";
    for (const char c : user) { if (c == '\'') quoted += "'\\''"; else quoted += c; }
    quoted += "'";
    std::string out;
    if (FILE* f = popen(("timeout 4 fprintd-list " + quoted + " 2>/dev/null").c_str(), "r")) {
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0 && out.size() < 4096) out.append(buf, n);
        pclose(f);
    }
    return out.find("ingerprints for user") != std::string::npos;  // ("Fingerprints for user ...:" lists them; spelled so, the i18n extractor does not take it for a text)
}

// One try waits for a finger (pam_fprintd gives up after its own timeout); after a miss or a timeout it tries again.
void fingerprintLoop(std::string user);
void startFingerprint() {
    const std::string user = g_profile.user;
    std::thread([user]() { fingerprintLoop(user); }).detach();
}

// A small fingerprint: arcs one inside the other, open at the bottom, and a short line in the middle.
void fingerprintGlyph(cairo_t* cr, double cx, double cy, double size, const Color& c, double alpha) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
    cairo_set_line_width(cr, size / 12);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const double deg = M_PI / 180;
    for (int i = 0; i < 4; i++) {
        const double r = size * (0.46 - 0.12 * i), open = (50 - 8 * i) * deg;  // (the inner arcs are open wider at the bottom)
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, M_PI / 2 + open, M_PI / 2 - open + 2 * M_PI);
        cairo_stroke(cr);
    }
    cairo_move_to(cr, cx, cy - size * 0.04);
    cairo_line_to(cr, cx, cy + size * 0.2);
    cairo_stroke(cr);
    cairo_restore(cr);
}

enum class LockAnim { None, SplitH, SplitV, Shape, Zoom };

struct Output {
    wl_output* output = nullptr;
    uint32_t name = 0;
    std::string connector;
    bool fullLockScreen = true, wallpaperDrawn = false;
    wl_surface* surface = nullptr;
    hidpi::SurfaceScale scale;
    ext_session_lock_surface_v1* lockSurface = nullptr;
    int w = 0, h = 0;
    bool configured = false;
    cairo_surface_t* cache = nullptr;  // the lock screen painted once, while it animates
    int cacheW = 0, cacheH = 0;
    bool cacheDirty = true, framePending = false;
    wl_callback* frameCb = nullptr;
    std::chrono::steady_clock::time_point frameAt;
};

struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fscaleMgr = nullptr;
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_pointer* pointer = nullptr;
    ext_session_lock_manager_v1* manager = nullptr;
    ext_session_lock_v1* lock = nullptr;
    std::vector<std::unique_ptr<Output>> outputs;
    std::vector<std::string> lockScreens;
    bool clock24 = true;
    std::array<std::string, 2> clockPeriods{"AM", "PM"};
    xkb_context* xkbCtx = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* xkbState = nullptr;
    bool locked = false, finished = false, done = false;
    std::string password;
    enum { Idle, Checking, Wrong } status = Idle;
    std::atomic<int> authResult{-1};
    std::atomic<bool> fingerprint{false};  // a reader with an enrolled finger was found: the finger unlocks too
    std::chrono::steady_clock::time_point lockedAt;
    LockAnim animMode = LockAnim::None;
    enum { AnimIdle, AnimEntering, AnimExiting } animPhase = AnimIdle;
    bool animStarted = false, unlocking = false;
    bool entryShown = false, firstFrame = false;  // entryShown: the first frame of coming in was painted (its clock starts there)
    std::chrono::steady_clock::time_point animStart;
    double animSeconds = 0.55, shapeStart = 0;
    std::string shapeName = "Cookie7Sided", baseRole;  // baseRole: the color outside the shape when the wallpaper is centered
} g;

std::map<std::string, cairo_surface_t*> g_bgCache;

// The video wallpaper behind the lock screen (Settings -> Desktop: "Play the video on the lock screen"). A layer surface such as mpvpaper's is hidden
// while the session is locked, so the video is decoded here: one ffmpeg per screen size writes raw frames of exactly that size into a pipe, and the
// newest complete one is what background() returns. Without ffmpeg, or when the video cannot be read, the still picture stays.
std::string g_videoPath;  // empty: the lock screen shows the picture
struct VideoFeed {
    pid_t pid = -1;
    int fd = -1, w = 0, h = 0;
    std::vector<unsigned char> acc;
    size_t have = 0;
    cairo_surface_t* frame = nullptr;
};
std::map<std::string, VideoFeed> g_feeds;
constexpr int kLockVideoFps = 24;

void startFeed(VideoFeed& f, int w, int h) {
    f.w = w;
    f.h = h;
    int p[2];
    if (pipe2(p, O_CLOEXEC) < 0) return;
    const pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return; }
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);  // (the player never outlives the lock screen)
        dup2(p[1], STDOUT_FILENO);
        const int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDIN_FILENO);
        dup2(nul, STDERR_FILENO);
        char vf[160];
        std::snprintf(vf, sizeof vf, "fps=%d,scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d", kLockVideoFps, w, h, w, h);
        execlp("ffmpeg", "ffmpeg", "-nostdin", "-loglevel", "quiet", "-re", "-stream_loop", "-1", "-i", g_videoPath.c_str(), "-an", "-vf", vf,
               "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1", nullptr);
        _exit(127);
    }
    close(p[1]);
    fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
    f.pid = pid;
    f.fd = p[0];
    f.acc.assign(static_cast<size_t>(w) * h * 4, 0);
}

// Reads what the players have written; true when a new picture is ready.
bool readFeeds() {
    bool fresh = false;
    for (auto& [key, f] : g_feeds) {
        if (f.fd < 0) continue;
        const size_t size = f.acc.size();
        for (;;) {
            const ssize_t n = read(f.fd, f.acc.data() + f.have, size - f.have);
            if (n > 0) {
                f.have += static_cast<size_t>(n);
                if (f.have < size) continue;
                f.have = 0;
                if (!f.frame) f.frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, f.w, f.h);
                cairo_surface_flush(f.frame);
                unsigned char* dst = cairo_image_surface_get_data(f.frame);
                const int stride = cairo_image_surface_get_stride(f.frame);
                for (int y = 0; y < f.h; y++) std::memcpy(dst + static_cast<size_t>(y) * stride, f.acc.data() + static_cast<size_t>(y) * f.w * 4, static_cast<size_t>(f.w) * 4);
                cairo_surface_mark_dirty(f.frame);
                fresh = true;
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EINTR)) { close(f.fd); f.fd = -1; }  // (ended: the last picture stays)
            break;
        }
    }
    return fresh;
}

// The still picture of the wallpaper at the size of a screen.
cairo_surface_t* stillBackground(int w, int h) {
    const std::string key = std::to_string(w) + "x" + std::to_string(h);
    if (auto it = g_bgCache.find(key); it != g_bgCache.end()) return it->second;
    std::string path = readFile(stateDir() + "/wallpaper.txt");
    while (!path.empty() && (path.back() == '\n' || path.back() == ' ')) path.pop_back();
    bool decoded = true;
    cairo_surface_t* small = path.empty() ? nullptr : cachedCover(path, w, h, cacheDir() + "/" + kBgPrefix + key + kPixSuffix, &decoded);
    g_bgCache[key] = small;
    mark(!small ? "wallpaper: none" : decoded ? "wallpaper ready (decoded from the picture)" : "wallpaper ready (read from the cache)");
    return small;
}

cairo_surface_t* background(int w, int h) {
    if (!g_videoPath.empty()) {
        VideoFeed& f = g_feeds[std::to_string(w) + "x" + std::to_string(h)];
        if (f.pid < 0) startFeed(f, w, h);
        if (f.frame) return f.frame;
    }
    return stillBackground(w, h);
}

// `end4pC-lock --warm`: after the wallpaper changed, decodes it once for every screen size a lock has used, so the next lock finds it ready.
void warmBackgrounds() {
    std::vector<std::pair<int, int>> sizes;
    if (DIR* d = opendir(cacheDir().c_str())) {
        while (const dirent* e = readdir(d)) {
            int w = 0, h = 0, end = 0;
            const std::string format = std::string(kBgPrefix) + "%dx%d" + kPixSuffix + "%n";
            if (std::sscanf(e->d_name, format.c_str(), &w, &h, &end) == 2 && end > 0 && e->d_name[end] == 0 && w > 0 && h > 0 && w <= 16384 && h <= 16384) sizes.emplace_back(w, h);
        }
        closedir(d);
    }
    for (const auto& [w, h] : sizes) stillBackground(w, h);
}

void onBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kBufferListener = {onBufferRelease};

// The wallpaper alone, as the desktop shows it.
void paintWallpaper(cairo_t* cr, int W, int H) {
    cairo_set_source_rgb(cr, 0.06, 0.06, 0.07);
    cairo_paint(cr);
    if (cairo_surface_t* bg = background(W, H)) {
        cairo_save(cr);
        cairo_scale(cr, double(W) / cairo_image_surface_get_width(bg), double(H) / cairo_image_surface_get_height(bg));
        cairo_set_source_surface(cr, bg, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

// ---- the player on the lock screen -------------------------------------------------------------------------------------------------------
// What plays now is asked from playerctl once a second by a thread. The title and the artist are never cut: the text wraps over as many lines as it
// needs and the type shrinks until it fits (Pango, word wrap, and by letter when one word is longer than the line).
struct Now {
    bool have = false;
    std::string status, instance, artist, title;
    double pos = 0, len = 0;  // seconds
    std::chrono::steady_clock::time_point at;
    std::string prevTitle, prevArtist;                 // the song before, while the new one slides in
    std::chrono::steady_clock::time_point changeAt{};  // when the song changed (the slide starts)
};
constexpr double kSlideSeconds = 0.5;
std::mutex g_nowMx;
Now g_now;
std::atomic<bool> g_playerOn{false};
struct PlayerHit { double x, y, w, h; int id; };
enum { kPlayPrev, kPlayToggle, kPlayNext, kPlaySeek };
std::map<std::string, std::vector<PlayerHit>> g_playerHits;  // by size of the output ("WxH"), filled when it is painted
int g_playerHover = -1;
struct SeekBar { double x = 0, w = 0; } g_seekBar;

Now nowSnapshot() {
    std::lock_guard<std::mutex> lk(g_nowMx);
    return g_now;
}
bool playerPlaying() {
    if (!g_playerOn) return false;
    const Now n = nowSnapshot();
    return n.have && n.status == "Playing";
}
// The song changed a moment ago: the new one is still sliding in.
bool playerSliding() {
    if (!g_playerOn) return false;
    const Now n = nowSnapshot();
    return n.have && n.changeAt != std::chrono::steady_clock::time_point{} && std::chrono::duration<double>(std::chrono::steady_clock::now() - n.changeAt).count() < kSlideSeconds;
}

void playerPoll() {
    std::string out;
    if (FILE* f = popen("timeout 2 playerctl -a metadata --format '{{status}}\t{{playerInstance}}\t{{position}}\t{{mpris:length}}\t{{artist}}\t{{title}}' 2>/dev/null", "r")) {
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0 && out.size() < 65536) out.append(buf, n);
        pclose(f);
    }
    Now best;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        size_t p = 0;
        for (int i = 0; i < 5; i++) {
            const size_t t = line.find('\t', p);
            if (t == std::string::npos) break;
            f.push_back(line.substr(p, t - p));
            p = t + 1;
        }
        if (f.size() < 5) continue;
        f.push_back(line.substr(p));
        if (f[0] != "Playing" && f[0] != "Paused") continue;
        if (best.have && best.status == "Playing") continue;
        if (best.have && f[0] != "Playing") continue;
        best.have = !f[5].empty();
        best.status = f[0];
        best.instance = f[1];
        best.pos = std::atof(f[2].c_str()) / 1e6;
        best.len = std::atof(f[3].c_str()) / 1e6;
        best.artist = f[4];
        best.title = f[5];
        best.at = std::chrono::steady_clock::now();
    }
    bool changed;
    {
        std::lock_guard<std::mutex> lk(g_nowMx);
        changed = g_now.have != best.have || g_now.title != best.title || g_now.artist != best.artist || g_now.status != best.status;
        const bool otherSong = best.have && (!g_now.have || g_now.title != best.title || g_now.artist != best.artist);
        if (otherSong) {  // (the first one after nothing was playing slides in too, with nothing to push out)
            best.prevTitle = g_now.have ? g_now.title : "";
            best.prevArtist = g_now.have ? g_now.artist : "";
            best.changeAt = std::chrono::steady_clock::now();
        } else {
            best.prevTitle = g_now.prevTitle;
            best.prevArtist = g_now.prevArtist;
            best.changeAt = g_now.changeAt;
        }
        g_now = best;
    }
    if (changed && g_wake[1] >= 0) { const char b = 'w'; (void)!write(g_wake[1], &b, 1); }
}
void playerLoop() {
    for (;;) {
        playerPoll();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
void startPlayer() {
    std::string off;
    jsonValue(readFile(configFile()), "lockPlayer", off);
    if (off == "0") return;
    g_playerOn = true;
    std::thread(playerLoop).detach();
}

// Runs playerctl for the player on show (twice forked: nobody waits for it).
void playerCommand(const std::vector<std::string>& args) {
    const Now n = nowSnapshot();
    if (!n.have || n.instance.empty()) return;
    const pid_t first = fork();
    if (first == 0) {
        if (fork() == 0) {
            std::vector<const char*> argv = {"playerctl", "-p", n.instance.c_str()};
            for (const std::string& a : args) argv.push_back(a.c_str());
            argv.push_back(nullptr);
            const int nul = open("/dev/null", O_RDWR);
            dup2(nul, 0); dup2(nul, 1); dup2(nul, 2);
            execvp("playerctl", const_cast<char* const*>(argv.data()));
            _exit(127);
        }
        _exit(0);
    }
    if (first > 0) waitpid(first, nullptr, 0);
}
void playerPress(int id, double fraction) {
    if (id == kPlayPrev) playerCommand({"previous"});
    else if (id == kPlayNext) playerCommand({"next"});
    else if (id == kPlayToggle) {
        playerCommand({"play-pause"});
        std::lock_guard<std::mutex> lk(g_nowMx);
        g_now.pos += g_now.status == "Playing" ? std::chrono::duration<double>(std::chrono::steady_clock::now() - g_now.at).count() : 0;
        g_now.at = std::chrono::steady_clock::now();
        g_now.status = g_now.status == "Playing" ? "Paused" : "Playing";
    } else if (id == kPlaySeek) {
        const Now n = nowSnapshot();
        if (n.len <= 0) return;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", std::clamp(fraction, 0.0, 1.0) * n.len);
        playerCommand({"position", buf});
        std::lock_guard<std::mutex> lk(g_nowMx);
        g_now.pos = std::clamp(fraction, 0.0, 1.0) * n.len;
        g_now.at = std::chrono::steady_clock::now();
    }
}

PangoLayout* blockLayout(cairo_t* cr, const std::string& s, double px, bool bold, double width, double spacingEm, double lineFactor, PangoWrapMode wrap = PANGO_WRAP_WORD_CHAR) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    PangoFontDescription* d = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_width(l, static_cast<int>(width * PANGO_SCALE));
    pango_layout_set_wrap(l, wrap);
    pango_layout_set_alignment(l, PANGO_ALIGN_RIGHT);
    pango_layout_set_line_spacing(l, lineFactor);
    if (spacingEm != 0) {
        PangoAttrList* a = pango_attr_list_new();
        pango_attr_list_insert(a, pango_attr_letter_spacing_new(static_cast<int>(spacingEm * px * PANGO_SCALE)));
        pango_layout_set_attributes(l, a);
        pango_attr_list_unref(a);
    }
    pango_layout_set_text(l, s.c_str(), -1);
    return l;
}
// The biggest type, from maxPx down to minPx, with which the text fits in maxH and no word has to be broken. If even minPx cannot keep the words whole
// (one very long word), the words are broken by letter at the biggest size that fits. The text is never cut.
PangoLayout* fitBlock(cairo_t* cr, const std::string& s, double width, bool bold, double maxPx, double minPx, double maxH, double spacingEm, double lineFactor, int& h) {
    for (double px = maxPx; px >= minPx; px -= 2) {
        PangoLayout* l = blockLayout(cr, s, px, bold, width, spacingEm, lineFactor, PANGO_WRAP_WORD);
        int w2 = 0;
        pango_layout_get_pixel_size(l, &w2, &h);
        if (w2 <= width + 1 && h <= maxH) return l;  // (a word wider than the line makes the layout wider than it)
        g_object_unref(l);
    }
    double px = maxPx;
    for (;;) {
        PangoLayout* l = blockLayout(cr, s, px, bold, width, spacingEm, lineFactor, PANGO_WRAP_WORD_CHAR);
        int w2 = 0;
        pango_layout_get_pixel_size(l, &w2, &h);
        if (h <= maxH || px <= minPx) return l;
        g_object_unref(l);
        px = std::max(minPx, px - 2);
    }
}
void blockPaint(cairo_t* cr, PangoLayout* l, double x, double y, const Color& c, double alpha, bool shadow) {
    if (shadow) {
        cairo_move_to(cr, x, y + 3);
        src(cr, Color{0, 0, 0}, 0.35);
        pango_cairo_show_layout(cr, l);
    }
    cairo_move_to(cr, x, y);
    src(cr, c, alpha);
    pango_cairo_show_layout(cr, l);
}

void waveProgress(cairo_t* cr, double x, double y, double w, double h, double frac, bool playing, const Color& c) {
    const double mid = y + h / 2, pw = std::clamp(frac, 0.0, 1.0) * w, per = 28, amp = 3.5;
    const double phase = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() * 2 * M_PI * 0.6;
    cairo_save(cr);
    cairo_set_line_width(cr, 3);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    src(cr, c);
    cairo_new_path(cr);
    const double end = std::max(0.0, pw - 4);
    for (double px = 0; px <= end; px += 1.5) {
        const double yy = mid + (playing ? amp : 0) * std::sin(px / per * 2 * M_PI - (playing ? phase : 0));
        if (px == 0) cairo_move_to(cr, x + px, yy); else cairo_line_to(cr, x + px, yy);
    }
    cairo_stroke(cr);
    src(cr, c, 0.28);
    cairo_move_to(cr, x + pw + 6, mid);
    cairo_line_to(cr, x + w, mid);
    cairo_stroke(cr);
    src(cr, c);
    rrect(cr, x + pw - 3, mid - h * 0.3, 6, h * 0.6, 3);
    cairo_fill(cr);
    cairo_restore(cr);
}

// The type of the song on the right side: a label, the title as big as it fits, the artist, and the progress line with the three buttons.
// When the song changes the old title and artist slide out to the left, fading, and the new ones slide in from the right.
struct PlayerText {
    PangoLayout* title = nullptr;
    PangoLayout* artist = nullptr;
    int th = 0, ah = 0;
    double top = 0, row = 0;  // where the block starts and where the line with the buttons goes, for this text
    ~PlayerText() { if (title) g_object_unref(title); if (artist) g_object_unref(artist); }
};
constexpr double kPlayerRowH = 52, kPlayerGap1 = 6, kPlayerGap2 = 8, kPlayerGap3 = 26;
void buildPlayerText(cairo_t* cr, PlayerText& t, const std::string& title, const std::string& artist, double width, double maxTitleH, int labelH, int H) {
    t.title = fitBlock(cr, title, width, true, 190, 26, maxTitleH, -0.03, 0.85, t.th);
    if (!artist.empty()) t.artist = fitBlock(cr, artist, width, false, 34, 18, 150, 0, 1.0, t.ah);
    const double total = labelH + kPlayerGap1 + t.th + (t.artist ? kPlayerGap2 + t.ah : 0) + kPlayerGap3 + kPlayerRowH;
    t.top = std::max(40.0, (H - total) / 2);
    t.row = t.top + total - kPlayerRowH;
}
void paintPlayerText(cairo_t* cr, const PlayerText& t, double top, double x, double dx, double alpha, int labelH) {
    const Color white{0.97, 0.97, 0.97};
    double y = top + labelH + kPlayerGap1;
    blockPaint(cr, t.title, x + dx, y, white, alpha, true);
    y += t.th;
    if (t.artist) blockPaint(cr, t.artist, x + dx, y + kPlayerGap2, white, 0.85 * alpha, false);
}

void paintPlayer(cairo_t* cr, int W, int H) {
    if (!g_playerOn) return;
    Now n = nowSnapshot();
    const std::string key = std::to_string(W) + "x" + std::to_string(H);
    if (!n.have) { g_playerHits.erase(key); return; }
    const auto now = std::chrono::steady_clock::now();
    const bool playing = n.status == "Playing";
    if (playing) n.pos += std::chrono::duration<double>(now - n.at).count();
    if (n.len > 0) n.pos = std::min(n.pos, n.len);
    const Color white{0.97, 0.97, 0.97};
    const double right = W - 96, width = std::min(620.0, W * 0.36), x0 = right - width;
    const double maxTitleH = std::min(H * 0.5, 520.0);

    std::string label = i18n::tr("Now playing");
    for (char& ch : label) if (static_cast<unsigned char>(ch) < 0x80) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    PangoLayout* ll = blockLayout(cr, label, 22, false, width, 0.2, 1.0);
    int lw = 0, lh = 0;
    pango_layout_get_pixel_size(ll, &lw, &lh);

    PlayerText now2;
    buildPlayerText(cr, now2, n.title, n.artist, width, maxTitleH, lh, H);
    double t = 1;  // how far the slide is (0 .. 1)
    if (n.changeAt != std::chrono::steady_clock::time_point{}) t = std::clamp(std::chrono::duration<double>(now - n.changeAt).count() / kSlideSeconds, 0.0, 1.0);
    const double e = 1 - std::pow(1 - t, 3);  // (ease out)
    const double slide = 90;
    double top = now2.top, row = now2.row;
    if (t < 1 && !n.prevTitle.empty()) {
        PlayerText old;
        buildPlayerText(cr, old, n.prevTitle, n.prevArtist, width, maxTitleH, lh, H);
        top = old.top + (now2.top - old.top) * e;
        row = old.row + (now2.row - old.row) * e;
        paintPlayerText(cr, old, top, x0, -slide * e, 1 - e, lh);
        paintPlayerText(cr, now2, top, x0, slide * (1 - e), e, lh);
    } else if (t < 1) {
        paintPlayerText(cr, now2, top, x0, slide * (1 - e), e, lh);
    } else {
        paintPlayerText(cr, now2, top, x0, 0, 1, lh);
    }
    const Color accent = color("primary");
    const bool haveAccent = accent.r + accent.g + accent.b > 0.9;  // (the fallback of color() is dark: it would not show on the veil)
    blockPaint(cr, ll, x0, top, haveAccent ? accent : white, haveAccent ? 1.0 : 0.7, false);
    g_object_unref(ll);

    // progress line on the left of the buttons, the buttons right aligned: previous, play / pause, next
    const double y = row, rowH = kPlayerRowH;
    const double side = 36, play = 52, bgap = 6, waveW = 260, waveGap = 20;
    const double bx = right - (side + bgap + play + bgap + side), wx = bx - waveGap - waveW;
    std::vector<PlayerHit> hits;
    waveProgress(cr, wx, y + (rowH - 24) / 2, waveW, 24, n.len > 0 ? n.pos / n.len : 0, playing, white);
    hits.push_back({wx - 4, y + (rowH - 36) / 2, waveW + 8, 36, kPlaySeek});
    g_seekBar = {wx, waveW};
    auto ghost = [&](double x, double d, int id, const char* ic) {
        const double cy = y + rowH / 2;
        if (g_playerHover == id) { src(cr, white, 0.16); cairo_arc(cr, x + d / 2, cy, d / 2, 0, 2 * M_PI); cairo_fill(cr); }
        icon(cr, ic, x + d / 2 - 14, cy - 14, 28, white, true);
        hits.push_back({x, cy - d / 2, d, d, id});
    };
    ghost(bx, side, kPlayPrev, "skip-previous");
    const double px0 = bx + side + bgap, cy = y + rowH / 2;
    src(cr, white, g_playerHover == kPlayToggle ? 1.0 : 0.94);
    cairo_arc(cr, px0 + play / 2, cy, play / 2, 0, 2 * M_PI);
    cairo_fill(cr);
    icon(cr, playing ? "pause" : "play", px0 + play / 2 - 13, cy - 13, 26, Color{0.11, 0.11, 0.11}, true);
    hits.push_back({px0, cy - play / 2, play, play, kPlayToggle});
    ghost(px0 + play + bgap, side, kPlayNext, "skip-next");
    g_playerHits[key] = hits;
}

void paintDimmedWallpaper(cairo_t* cr, int W, int H) {
    paintWallpaper(cr, W, H);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.38);
    cairo_paint(cr);
}

// The full lock screen adds the clock, weather, avatar, player and password box.
void paintFrame(cairo_t* cr, int W, int H) {
    paintDimmedWallpaper(cr, W, H);
    paintPlayer(cr, W, H);

    const Color white{0.97, 0.97, 0.97};
    const double M = 72;
    double y = H - M;

    {
        const double pw = 320, ph = 52;
        y -= ph;
        rrect(cr, M, y, pw, ph, ph / 2);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.14);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.22);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        icon(cr, g.status == App::Wrong ? "danger-circle" : "lock-keyhole-minimalistic", M + 16, y + (ph - 22) / 2, 22, g.status == App::Wrong ? Color{1, 0.6, 0.6} : white);
        const double tx = M + 52;
        if (g.fingerprint) fingerprintGlyph(cr, M + pw - 28, y + ph / 2.0, 26, white, g.status == App::Checking ? 0.4 : 0.75);
        if (g.status == App::Checking) text(cr, "Checking…", tx, y + (ph - 20) / 2 - 2, 18, false, white, 0.8);
        else if (g.status == App::Wrong && g.password.empty()) text(cr, "Wrong password", tx, y + (ph - 20) / 2 - 2, 18, false, Color{1, 0.6, 0.6});
        else if (g.password.empty()) text(cr, "Password", tx, y + (ph - 20) / 2 - 2, 18, false, white, 0.6);
        else {
            size_t chars = 0;
            for (unsigned char c : g.password) if ((c & 0xC0) != 0x80) chars++;
            const size_t fit = static_cast<size_t>((pw - 52 - 20) / 16);
            const size_t shown = std::min(chars, fit);
            for (size_t i = 0; i < shown; i++) {
                cairo_arc(cr, tx + 6 + i * 16, y + ph / 2.0, 4.5, 0, 2 * M_PI);
                src(cr, white);
                cairo_fill(cr);
            }
        }
    }

    Weather w;
    {
        std::lock_guard<std::mutex> lk(g_weatherMx);
        w = g_weather;
    }
    y -= 26;
    if (w.ok) {
        std::string label, ic;
        describe(w, label, ic);
        y -= 32;
        icon(cr, ic, M, y + 2, 28, white);
        char t[32];
        std::snprintf(t, sizeof t, "%.0f°%c", w.temp, w.fahrenheit ? 'F' : 'C');
        const TextSize ts = text(cr, t, M + 40, y, 24, true, white);
        text(cr, label + (w.city.empty() ? "" : "  ·  " + w.city), M + 40 + ts.w + 14, y + 4, 18, false, white, 0.75);
        y -= 8;
    }

    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    char buf[64];
    i18n::strftime(buf, sizeof buf, "%A, %e %B", &lt);
    {
        std::string d = buf;
        const size_t dbl = d.find("  ");
        if (dbl != std::string::npos) d.erase(dbl, 1);
        y -= 28;
        text(cr, d, M + 4, y, 22, false, white, 0.85);
        y -= 4;
    }
    std::strftime(buf, sizeof buf, g.clock24 ? "%H:%M" : "%-I:%M", &lt);
    {
        constexpr double xGap = 12;
        const double x = M - 4;
        double size = 132, periodSize = 32;
        TextSize ts = text(cr, buf, 0, 0, size, true, white, 1, false);
        const std::string& period = g.clockPeriods[lt.tm_hour >= 12];
        TextSize ps;
        if (!g.clock24) {
            ps = text(cr, period, 0, 0, periodSize, true, white, 1, false);
            const double available = W - x - xGap - 16;
            const double scale = std::clamp(available / (ts.w + ps.w), 0.2, 1.0);
            if (scale < 1) {
                size *= scale;
                periodSize *= scale;
                ts = text(cr, buf, 0, 0, size, true, white, 1, false);
                ps = text(cr, period, 0, 0, periodSize, true, white, 1, false);
            }
        }
        y -= ts.h - 26;
        text(cr, buf, x, y, size, true, white);
        if (!g.clock24) text(cr, period, x + ts.w + xGap, y + ts.baseline - ps.baseline, periodSize, true, white);
    }

    {
        y -= kAvatar - 10;
        const double cx = M + kAvatar / 2.0, cy = y + kAvatar / 2.0;
        cairo_arc(cr, cx, cy, kAvatar / 2.0 + 4, 0, 2 * M_PI);
        src(cr, color("primary"));
        cairo_fill(cr);
        cairo_save(cr);
        cairo_arc(cr, cx, cy, kAvatar / 2.0, 0, 2 * M_PI);
        cairo_clip(cr);
        if (g_profile.avatar) {
            cairo_set_source_surface(cr, g_profile.avatar, M, y);
            cairo_paint(cr);
        } else {
            src(cr, color("primary_container"));
            cairo_paint(cr);
            icon(cr, "user-circle", cx - 28, cy - 28, 56, color("on_primary_container"));
        }
        cairo_restore(cr);
        std::string up;
        {
            double secs = 0;
            std::ifstream f("/proc/uptime");
            f >> secs;
            const int total = static_cast<int>(secs) / 60, d = total / 1440, h = (total % 1440) / 60, m = total % 60;
            if (d > 0) up += std::to_string(d) + "d";
            if (h > 0) up += (up.empty() ? "" : ", ") + std::to_string(h) + "h";
            if (m > 0 || up.empty()) up += (up.empty() ? "" : ", ") + std::to_string(m) + "m";
            up = "Up • " + up;
        }
        const double nameX = M + kAvatar + 22, maxName = std::max(120.0, std::min(W * 0.42, 560.0) - nameX);
        std::string shownName = g_profile.name;  // (a long name ends in "…" before it runs off the card)
        for (bool cut = false; shownName.size() > 1 && text(cr, shownName + (cut ? "…" : ""), 0, 0, 26, true, white, 1, false).w > maxName; cut = true) {
            size_t k = shownName.size() - 1;
            while (k > 0 && (static_cast<unsigned char>(shownName[k]) & 0xC0) == 0x80) k--;
            shownName.resize(k);
            if (shownName.size() <= 1 || text(cr, shownName + "…", 0, 0, 26, true, white, 1, false).w <= maxName) { shownName += "…"; break; }
        }
        const TextSize nameS = text(cr, shownName, 0, 0, 26, true, white, 1, false);
        const TextSize upS = text(cr, up, 0, 0, 16, false, white, 1, false);
        const double top = cy - (nameS.h + upS.h - 2) / 2.0, tx = M + kAvatar + 22;
        text(cr, shownName, tx, top, 26, true, white);
        text(cr, up, tx + 1, top + nameS.h - 2, 16, false, white, 0.75);
    }

}

// ---- how the lock screen comes in and goes out (cfg lockAnimation) ----------------------------------------------------------------
double easeOutCubic(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

// Zoom comes in with a small rebound: it goes a little past the end (about 4%) and settles back.
double easeOutBack(double t) {
    constexpr double c1 = 1.0, c3 = c1 + 1;
    t = std::clamp(t, 0.0, 1.0) - 1;
    return 1 + c3 * t * t * t + c1 * t * t;
}

double entryOpen(double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (g.animMode == LockAnim::Zoom) return easeOutBack(t);
    if (g.animMode == LockAnim::Shape) return std::min(1.0, t * t * (3 - 2 * t) * 1.15);
    return easeOutCubic(t);
}

double exitOpen(double t) { return easeOutCubic(1 - t); }

double animOpen() {
    if (g.animPhase == App::AnimIdle) return 1;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g.animStart).count() / g.animSeconds;
    return g.animPhase == App::AnimEntering ? entryOpen(t) : exitOpen(t);
}

constexpr double kSeamAlpha = 0;

// The frame (already painted in cache) as it shows when the animation is open of 0..1.
void compose(cairo_t* cr, int W, int H, cairo_surface_t* cache, double open) {
    const Color base = g.baseRole.empty() ? Color{0, 0, 0} : color(g.baseRole);
    src(cr, base);
    cairo_paint(cr);
    if (g.animMode == LockAnim::SplitH || g.animMode == LockAnim::SplitV) {
        paintWallpaper(cr, W, H);  // (what is not covered yet is the wallpaper, as the desktop shows it, and not black: the halves come over it with their veil)
        const bool horizontal = g.animMode == LockAnim::SplitH;  // (the halves are the top and the bottom one)
        const double len = horizontal ? H : W, off = (1 - open) * len / 2.0;
        auto half = [&](int k) {  // 0: top or left, 1: bottom or right
            const double d = k == 0 ? -off : off, o0 = k == 0 ? 0 : len / 2.0;
            const double x = horizontal ? 0 : o0 + d, y = horizontal ? o0 + d : 0;
            cairo_set_source_surface(cr, cache, horizontal ? 0 : d, horizontal ? d : 0);
            cairo_rectangle(cr, x, y, horizontal ? W : len / 2.0, horizontal ? len / 2.0 : H);
            cairo_fill(cr);
        };
        half(1);
        half(0);
        if (kSeamAlpha > 0 && off > 0.5) {
            const double a = kSeamAlpha * std::min(1.0, off / 60.0);
            for (int k = 0; k < 2; k++) {
                const double e = len / 2.0 + (k == 0 ? -off : off) - 1.5;
                cairo_set_source_rgba(cr, 1, 1, 1, a);
                if (horizontal) cairo_rectangle(cr, 0, e, W, 3); else cairo_rectangle(cr, e, 0, 3, H);
                cairo_fill(cr);
            }
        }
        return;
    }
    if (g.animMode == LockAnim::Zoom) {  // the lock screen settles from a bigger picture, fading in over the desktop (and the other way when it goes out)
        paintWallpaper(cr, W, H);
        constexpr double kZoomFrom = 0.12;
        const double scale = 1 + kZoomFrom * (1 - open), alpha = std::clamp(open * 1.6, 0.0, 1.0);
        cairo_save(cr);
        cairo_translate(cr, W / 2.0, H / 2.0);
        cairo_scale(cr, scale, scale);
        cairo_translate(cr, -W / 2.0, -H / 2.0);
        cairo_set_source_surface(cr, cache, 0, 0);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);  // (the rebound goes under 1: the edge pixels fill what the picture no longer covers)
        cairo_paint_with_alpha(cr, alpha);
        cairo_restore(cr);
        return;
    }
    if (g.animMode == LockAnim::Shape) {
        const double full = std::hypot(double(W), double(H)) * 1.4, size = g.shapeStart + (full - g.shapeStart) * std::clamp(open, 0.0, 1.0);
        if (size < 2) return;
        cairo_save(cr);
        cairo_translate(cr, W / 2.0, H / 2.0);
        cairo_rotate(cr, (1 - open) * 0.6);
        shapes::path(cr, shapes::table(assetsDir()), g.shapeName, -size / 2, -size / 2, size);
        cairo_restore(cr);  // (the path keeps the rotation it was built with)
        cairo_clip(cr);
        cairo_set_source_surface(cr, cache, 0, 0);
        cairo_paint(cr);
    }
}

void frameDone(void* d, wl_callback* cb, uint32_t) {
    Output* o = static_cast<Output*>(d);
    o->framePending = false;
    o->frameCb = nullptr;
    wl_callback_destroy(cb);
}
const wl_callback_listener kFrameListener = {frameDone};

void draw(Output& o) {
    if (!o.configured || o.w <= 0 || o.h <= 0) return;
    if (!o.fullLockScreen && o.wallpaperDrawn) return;
    const bool animating = o.fullLockScreen && g.animPhase != App::AnimIdle;
    if (animating && o.framePending) return;  // (the last frame has not been shown yet)
    const int W = o.w, H = o.h;
    const double sc = o.scale.factor();
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, sc, "end4pC-lock")) return;
    cairo_t* cr = mkcr(hf.surface);

    if (!o.fullLockScreen) {
        paintDimmedWallpaper(cr, W, H);
    } else if (animating) {
        if (!o.cache || o.cacheW != W || o.cacheH != H || o.cacheDirty || cairo_image_surface_get_width(o.cache) != hf.pw) {
            if (o.cache) cairo_surface_destroy(o.cache);
            o.cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, hf.pw, hf.ph);  // (at the buffer's pixels, painting in logical units)
            cairo_surface_set_device_scale(o.cache, sc, sc);
            cairo_t* cc = mkcr(o.cache);
            paintFrame(cc, W, H);
            cairo_destroy(cc);
            o.cacheW = W;
            o.cacheH = H;
            o.cacheDirty = false;
        }
        // The animation of coming in begins with the first frame that is shown, and not when the screen was ready: what it took to paint that frame
        // (the wallpaper, the type) is not taken from it.
        if (g.animPhase == App::AnimEntering && !g.entryShown) { g.entryShown = true; g.animStart = std::chrono::steady_clock::now(); }
        compose(cr, W, H, o.cache, animOpen());
        o.framePending = true;
        o.frameAt = std::chrono::steady_clock::now();
        o.frameCb = wl_surface_frame(o.surface);
        wl_callback_add_listener(o.frameCb, &kFrameListener, &o);
    } else {
        paintFrame(cr, W, H);
    }

    cairo_destroy(cr);
    if (!g.firstFrame) mark("first frame painted");
    wl_buffer* buffer = hidpi::buffer(hf, g.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    hidpi::present(o.surface, buffer, hf, o.scale);
    if (!g.firstFrame) { g.firstFrame = true; mark("first frame committed"); }
    o.wallpaperDrawn = !o.fullLockScreen;
}
void drawAll() { for (auto& o : g.outputs) draw(*o); }

void updateLockOutputs() {
    const auto selected = [](const Output& output) {
        return !output.connector.empty() && std::find(g.lockScreens.begin(), g.lockScreens.end(), output.connector) != g.lockScreens.end();
    };
    const bool matched = std::any_of(g.outputs.begin(), g.outputs.end(), [&](const auto& output) { return selected(*output); });
    for (auto& output : g.outputs) {
        const bool fullLockScreen = !matched || output->connector.empty() || selected(*output);
        if (fullLockScreen == output->fullLockScreen) continue;
        output->fullLockScreen = fullLockScreen;
        output->wallpaperDrawn = false;
        output->cacheDirty = true;
        if (output->frameCb) wl_callback_destroy(output->frameCb);
        output->frameCb = nullptr;
        output->framePending = false;
        if (!fullLockScreen && output->cache) {
            cairo_surface_destroy(output->cache);
            output->cache = nullptr;
        }
        draw(*output);
    }
}

void onLockSurfaceConfigure(void* d, ext_session_lock_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
    auto* o = static_cast<Output*>(d);
    ext_session_lock_surface_v1_ack_configure(ls, serial);
    o->w = static_cast<int>(w);
    o->h = static_cast<int>(h);
    o->configured = true;
    o->cacheDirty = true;
    o->wallpaperDrawn = false;
    if (o->fullLockScreen && !g.animStarted) {  // the first screen that is ready starts the animation of coming in
        mark("first screen configured");
        g.animStarted = true;
        if (g.animMode != LockAnim::None) { g.animPhase = App::AnimEntering; g.animStart = std::chrono::steady_clock::now(); }
    }
    draw(*o);
}
const ext_session_lock_surface_v1_listener kLockSurfaceListener = {onLockSurfaceConfigure};

void createLockSurface(Output& o) {
    if (!g.lock || o.lockSurface) return;
    o.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(o.scale, o.surface, g.viewporter, g.fscaleMgr, [op = &o] { op->cacheDirty = true; op->wallpaperDrawn = false; draw(*op); });
    o.lockSurface = ext_session_lock_v1_get_lock_surface(g.lock, o.surface, o.output);
    ext_session_lock_surface_v1_add_listener(o.lockSurface, &kLockSurfaceListener, &o);
}

// The shell stops the video wallpaper while this runs: a marker with our pid, and a nudge to the shell (it looks at the marker).
std::string lockMarker() {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    return std::string(rt ? rt : "/tmp") + "/end4pC-locked";
}
void tellShell() {
    char exe[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    std::string shell = exe;
    shell = shell.substr(0, shell.rfind('/')) + "/end4pC";
    if (access(shell.c_str(), X_OK) != 0) return;
    const pid_t first = fork();
    if (first == 0) {  // (twice: nobody has to wait for the shell's answer)
        if (fork() == 0) {
            const int nul = open("/dev/null", O_RDWR);
            dup2(nul, STDOUT_FILENO);
            dup2(nul, STDERR_FILENO);
            execl(shell.c_str(), shell.c_str(), "videosync", nullptr);
        }
        _exit(0);
    }
    if (first > 0) waitpid(first, nullptr, 0);
}
void onLocked(void*, ext_session_lock_v1*) {
    g.locked = true;
    g.lockedAt = std::chrono::steady_clock::now();
    mark("the compositor says the session is locked");
    if (FILE* m = std::fopen(lockMarker().c_str(), "w")) { std::fprintf(m, "%d\n", static_cast<int>(getpid())); std::fclose(m); }
    tellShell();
}
void onFinished(void*, ext_session_lock_v1*) { g.finished = true; }
const ext_session_lock_v1_listener kLockListener = {onLocked, onFinished};

void onKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    xkb_keymap* km = xkb_keymap_new_from_string(g.xkbCtx, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!km) return;
    if (g.xkbState) xkb_state_unref(g.xkbState);
    if (g.keymap) xkb_keymap_unref(g.keymap);
    g.keymap = km;
    g.xkbState = xkb_state_new(km);
}
void onKbEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void onKbLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
void onKbModifiers(void*, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    if (g.xkbState) xkb_state_update_mask(g.xkbState, depressed, latched, locked, 0, 0, group);
}
void onKbRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

void fingerprintLoop(std::string user) {
    if (!fpEnrolled(user)) return;
    if (fpModule().empty()) { std::fprintf(stderr, "end4pC-lock: fingerprint: the finger is enrolled but pam_fprintd.so is not installed (package fprintd, or libpam-fprintd)\n"); return; }
    const std::string dir = fpConfDir();
    if (dir.empty()) { std::fprintf(stderr, "end4pC-lock: fingerprint: could not write the PAM stack in XDG_RUNTIME_DIR\n"); return; }
    g.fingerprint = true;
    if (g_wake[1] >= 0) { const char b = 'w'; (void)!write(g_wake[1], &b, 1); }
    int quick = 0;
    while (!g.done && !g.unlocking) {
        const auto began = std::chrono::steady_clock::now();
        pam_conv conv = {fpConv, nullptr};
        pam_handle_t* h = nullptr;
        int rc = pam_start_confdir(kFpService, user.c_str(), &conv, dir.c_str(), &h);
        if (rc == PAM_SUCCESS) {
            rc = pam_authenticate(h, 0);
            pam_end(h, rc);
        }
        if (rc == PAM_SUCCESS) {
            g.authResult = 2;  // (2: the finger; no password to unlock the keyring with)
            const char b = 'a';
            (void)!write(g_wake[1], &b, 1);
            return;
        }
        if (quick == 0 && rc != PAM_SUCCESS && rc != PAM_AUTH_ERR && rc != PAM_MAXTRIES) std::fprintf(stderr, "end4pC-lock: fingerprint: PAM answered %d\n", rc);
        // A reader that fails at once, again and again (unplugged, fprintd gone): give up instead of spinning.
        quick = std::chrono::steady_clock::now() - began < std::chrono::seconds(2) ? quick + 1 : 0;
        if (quick >= 5) { g.fingerprint = false; if (g_wake[1] >= 0) { const char b = 'w'; (void)!write(g_wake[1], &b, 1); } return; }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void submit() {
    if (g.password.empty() || g.status == App::Checking) return;
    g.status = App::Checking;
    g.authResult = -1;
    std::string pw = g.password;
    const std::string user = g_profile.user;
    g.password.clear();
    std::thread([pw, user]() mutable {
        const bool ok = pamCheck(user, pw);
        explicit_bzero(pw.data(), pw.size());
        g.authResult = ok ? 1 : 0;
        const char b = 'a';
        (void)!write(g_wake[1], &b, 1);
    }).detach();
    drawAll();
}

void onKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !g.xkbState || g.status == App::Checking || g.unlocking) return;
    const uint32_t code = key + 8;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, code);
    const bool ctrl = xkb_state_mod_name_is_active(g.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (g.status == App::Wrong) g.status = App::Idle;
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) { submit(); return; }
    if (sym == XKB_KEY_Escape || (ctrl && (sym == XKB_KEY_BackSpace || sym == XKB_KEY_u || sym == XKB_KEY_U))) {
        explicit_bzero(g.password.data(), g.password.size());
        g.password.clear();
    } else if (sym == XKB_KEY_BackSpace) {
        while (!g.password.empty() && (static_cast<unsigned char>(g.password.back()) & 0xC0) == 0x80) g.password.pop_back();
        if (!g.password.empty()) g.password.pop_back();
    } else if (!ctrl) {
        char buf[16];
        const int n = xkb_state_key_get_utf8(g.xkbState, code, buf, sizeof buf);
        if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f) return;
        g.password.append(buf, n);
    } else {
        return;
    }
    drawAll();
}
const wl_keyboard_listener kKeyboardListener = {onKeymap, onKbEnter, onKbLeave, onKey, onKbModifiers, onKbRepeatInfo};

// ---- pointer: only the buttons of the player --------------------------------------------------------------------------------------------
wl_surface* g_ptrSurface = nullptr;
double g_ptrX = 0, g_ptrY = 0;
const std::vector<PlayerHit>* playerHitsHere() {
    if (!g_playerOn) return nullptr;
    for (const auto& o : g.outputs)
        if (o->fullLockScreen && o->surface == g_ptrSurface) {
            const auto it = g_playerHits.find(std::to_string(o->w) + "x" + std::to_string(o->h));
            return it == g_playerHits.end() ? nullptr : &it->second;
        }
    return nullptr;
}
int playerHitAt(double x, double y) {
    if (const auto* hits = playerHitsHere())
        for (const PlayerHit& h : *hits)
            if (x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h) return h.id;
    return -1;
}
void onPtrEnter(void*, wl_pointer*, uint32_t, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) { g_ptrSurface = surface; g_ptrX = wl_fixed_to_double(x); g_ptrY = wl_fixed_to_double(y); }
void onPtrLeave(void*, wl_pointer*, uint32_t, wl_surface*) {
    g_ptrSurface = nullptr;
    if (g_playerHover != -1) { g_playerHover = -1; drawAll(); }
}
void onPtrMotion(void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
    g_ptrX = wl_fixed_to_double(x);
    g_ptrY = wl_fixed_to_double(y);
    const int hover = playerHitAt(g_ptrX, g_ptrY);
    if (hover != g_playerHover) { g_playerHover = hover; drawAll(); }
}
void onPtrButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    if (state != WL_POINTER_BUTTON_STATE_PRESSED || button != 0x110 || g.unlocking) return;
    const int id = playerHitAt(g_ptrX, g_ptrY);
    if (id < 0) return;
    playerPress(id, g_seekBar.w > 0 ? (g_ptrX - g_seekBar.x) / g_seekBar.w : 0);
    drawAll();
}
// (libwayland aborts the program when an event arrives that has no function: every one of them needs one, the scroll ones do nothing)
void onPtrAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
void onPtrFrame(void*, wl_pointer*) {}
void onPtrAxisSource(void*, wl_pointer*, uint32_t) {}
void onPtrAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void onPtrAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
void onPtrAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
void onPtrAxisDirection(void*, wl_pointer*, uint32_t, uint32_t) {}
void onPtrWarp(void*, wl_pointer*, wl_fixed_t, wl_fixed_t) {}
wl_pointer_listener makePointerListener() {
    wl_pointer_listener l{};
    l.enter = onPtrEnter;
    l.leave = onPtrLeave;
    l.motion = onPtrMotion;
    l.button = onPtrButton;
    l.axis = onPtrAxis;
    l.frame = onPtrFrame;
    l.axis_source = onPtrAxisSource;
    l.axis_stop = onPtrAxisStop;
    l.axis_discrete = onPtrAxisDiscrete;
    l.axis_value120 = onPtrAxisValue120;
    l.axis_relative_direction = onPtrAxisDirection;
    l.warp = onPtrWarp;
    return l;
}
const wl_pointer_listener kPointerListener = makePointerListener();

void onSeatCaps(void*, wl_seat* seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g.keyboard) {
        g.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g.keyboard, &kKeyboardListener, nullptr);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g.pointer) {
        g.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g.pointer, &kPointerListener, nullptr);
    }
}
void onSeatName(void*, wl_seat*, const char*) {}
const wl_seat_listener kSeatListener = {onSeatCaps, onSeatName};

const wl_output_listener kLockOutputListener = {
    [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
    [](void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {},
    [](void*, wl_output*) {},
    [](void*, wl_output*, int32_t) {},
    [](void* data, wl_output*, const char* name) {
        static_cast<Output*>(data)->connector = name ? name : "";
        updateLockOutputs();
    },
    [](void*, wl_output*, const char*) {},
};

void onGlobal(void*, wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
    if (!std::strcmp(iface, wl_compositor_interface.name)) g.compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    else if (!std::strcmp(iface, wl_shm_interface.name)) g.shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    else if (!std::strcmp(iface, wp_viewporter_interface.name) && !hidpi::disabled())  // (sharp drawing at fractional scales, hidpi.h)
        g.viewporter = static_cast<wp_viewporter*>(wl_registry_bind(reg, name, &wp_viewporter_interface, 1));
    else if (!std::strcmp(iface, wp_fractional_scale_manager_v1_interface.name) && !hidpi::disabled())
        g.fscaleMgr = static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1));
    else if (!std::strcmp(iface, wl_seat_interface.name)) {
        g.seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(g.seat, &kSeatListener, nullptr);
    } else if (!std::strcmp(iface, ext_session_lock_manager_v1_interface.name)) {
        g.manager = static_cast<ext_session_lock_manager_v1*>(wl_registry_bind(reg, name, &ext_session_lock_manager_v1_interface, 1));
    } else if (!std::strcmp(iface, wl_output_interface.name)) {
        auto o = std::make_unique<Output>();
        o->name = name;
        o->output = static_cast<wl_output*>(wl_registry_bind(reg, name, &wl_output_interface, std::min(version, 4u)));
        Output* raw = o.get();
        wl_output_add_listener(raw->output, &kLockOutputListener, raw);
        g.outputs.push_back(std::move(o));
        createLockSurface(*raw);
    }
}
void onGlobalRemove(void*, wl_registry*, uint32_t name) {
    for (auto it = g.outputs.begin(); it != g.outputs.end(); ++it)
        if ((*it)->name == name) {
            Output& o = **it;
            if (o.frameCb) wl_callback_destroy(o.frameCb);
            if (o.cache) cairo_surface_destroy(o.cache);
            if (o.lockSurface) ext_session_lock_surface_v1_destroy(o.lockSurface);
            hidpi::detach(o.scale);
            if (o.surface) wl_surface_destroy(o.surface);
            if (o.output) wl_output_destroy(o.output);
            g.outputs.erase(it);
            updateLockOutputs();
            return;
        }
}
const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};

void unlockAndExit() {
    ext_session_lock_v1_unlock_and_destroy(g.lock);
    g.lock = nullptr;
    wl_display_roundtrip(g.display);
    unlink(lockMarker().c_str());
    tellShell();
    g.done = true;
}

// The password was right: the screen goes out with its animation (the session stays locked until it ends), then it unlocks.
void startUnlock() {
    bool shown = false;
    for (const auto& o : g.outputs) shown = shown || o->configured;
    if (g.animMode == LockAnim::None || !shown) { unlockAndExit(); return; }
    g.unlocking = true;
    g.animPhase = App::AnimExiting;
    g.animStart = std::chrono::steady_clock::now();
    for (auto& o : g.outputs) { o->cacheDirty = true; o->framePending = false; }
    drawAll();
}

// Moves the animation on: false when there is none.
void animTick(bool& redraw) {
    if (g.animPhase == App::AnimIdle) return;
    const auto now = std::chrono::steady_clock::now();
    for (auto& o : g.outputs)
        if (o->framePending && now - o->frameAt > std::chrono::milliseconds(120)) o->framePending = false;  // (no frame callback came: do not stall)
    if (std::chrono::duration<double>(now - g.animStart).count() < g.animSeconds) { redraw = true; return; }
    if (g.animPhase == App::AnimExiting) { unlockAndExit(); return; }
    g.animPhase = App::AnimIdle;
    for (auto& o : g.outputs) {
        if (o->cache) { cairo_surface_destroy(o->cache); o->cache = nullptr; }
        o->framePending = false;
    }
    redraw = true;
}
}

// The animation of coming in and going out, from the config (off when the shell's animations are off).
void readAnimConfig() {
    const std::string cfg = readFile(configFile());
    std::string v;
    g.animMode = LockAnim::None;
    if (jsonValue(cfg, "lockAnimation", v))
        g.animMode = v == "split_h" ? LockAnim::SplitH : v == "split_v" ? LockAnim::SplitV : v == "shape" ? LockAnim::Shape : v == "zoom" ? LockAnim::Zoom : LockAnim::None;
    if (jsonValue(cfg, "animEnabled", v) && v == "0") g.animMode = LockAnim::None;
    double speed = 100;
    if (jsonValue(cfg, "animSpeed", v) && !v.empty()) speed = std::clamp(std::atof(v.c_str()), 50.0, 200.0);
    g.animSeconds = (g.animMode == LockAnim::Shape ? 0.75 : g.animMode == LockAnim::Zoom ? 0.7 : 0.55) * 100.0 / speed;
    if (jsonValue(cfg, "lockVideo", v) && v == "1") {
        std::string path = readFile(stateDir() + "/video-wallpaper.txt");
        while (!path.empty() && (path.back() == '\n' || path.back() == ' ')) path.pop_back();
        if (!path.empty() && access(path.c_str(), R_OK) == 0) g_videoPath = path;
    }
    if (jsonValue(cfg, "centeredWallpaperShape", v) && !v.empty()) g.shapeName = v;
    if (jsonValue(cfg, "centeredWallpaper", v) && v == "1") {  // the shape starts as the one the desktop shows, and outside it is the desktop's color
        g.baseRole = "primary_container";
        if (jsonValue(cfg, "centeredWallpaperColor", v) && !v.empty()) g.baseRole = v;
        g.shapeStart = 400;
        if (jsonValue(cfg, "centeredWallpaperSize", v) && !v.empty()) g.shapeStart = std::clamp(std::atof(v.c_str()), 400.0, 800.0);
    }
}

// `end4pC-lock --dump-frames DIR`: writes pictures of every animation at some points of it (no lock is taken), to look at them.
void dumpFrames(const std::string& dir) {
    constexpr int W = 1280, H = 720;
    cairo_surface_t* cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t* cc = mkcr(cache);
    paintFrame(cc, W, H);
    cairo_destroy(cc);
    struct Case { const char* name; LockAnim mode; bool centered; };
    for (const Case& c : {Case{"split-h", LockAnim::SplitH, false}, Case{"split-v", LockAnim::SplitV, false}, Case{"shape", LockAnim::Shape, false}, Case{"shape-centered", LockAnim::Shape, true}, Case{"zoom", LockAnim::Zoom, false}}) {
        g.animMode = c.mode;
        g.baseRole = c.centered ? "primary_container" : "";
        g.shapeStart = c.centered ? 400 : 0;
        for (int i = 0; i <= 6; i++) {
            cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
            cairo_t* cr = mkcr(s);
            compose(cr, W, H, cache, entryOpen(i / 6.0));
            cairo_destroy(cr);
            cairo_surface_write_to_png(s, (dir + "/" + c.name + "-" + std::to_string(i) + ".png").c_str());
            cairo_surface_destroy(s);
        }
    }
    cairo_surface_destroy(cache);
}

// Development: paints the lock screen with songs of a file (lines: name TAB title TAB artist; short and very long names are the interesting ones)
// into PNG files: end4pC-lock --dump-player DIR, with DIR/songs.tsv
void dumpPlayer(const std::string& dir) {
    constexpr int W = 1920, H = 1080;
    std::ifstream in(dir + "/songs.tsv");
    g_playerOn = true;
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        size_t p = 0;
        for (int i = 0; i < 2; i++) {
            const size_t t = line.find('\t', p);
            if (t == std::string::npos) break;
            f.push_back(line.substr(p, t - p));
            p = t + 1;
        }
        if (f.size() < 2) continue;
        f.push_back(line.substr(p));
        {
            std::lock_guard<std::mutex> lk(g_nowMx);
            g_now = {};
            g_now.have = true;
            g_now.status = "Playing";
            g_now.instance = "test";
            g_now.title = f[1];
            g_now.artist = f[2];
            g_now.pos = 100;
            g_now.len = 260;
            g_now.at = std::chrono::steady_clock::now();
            // optional 4th and 5th columns: the song before, and how far the slide is (0 .. 1)
            std::vector<std::string> extra;
            for (size_t q = f[2].find('\t'); q != std::string::npos; q = f[2].find('\t')) { extra.push_back(f[2].substr(q + 1)); f[2].resize(q); break; }
            g_now.artist = f[2];
            if (!extra.empty()) {
                const size_t t2 = extra[0].find('\t');
                g_now.prevTitle = extra[0].substr(0, t2);
                const double progress = t2 == std::string::npos ? 0.4 : std::atof(extra[0].c_str() + t2 + 1);
                g_now.changeAt = g_now.at - std::chrono::milliseconds(static_cast<int>(progress * kSlideSeconds * 1000));
            }
        }
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        cairo_t* cr = mkcr(s);
        paintFrame(cr, W, H);
        cairo_destroy(cr);
        cairo_surface_write_to_png(s, (dir + "/player-" + f[0] + ".png").c_str());
        cairo_surface_destroy(s);
    }
}

void readClockConfig(const std::string& config) {
    std::string clock24;
    g.clock24 = !jsonValue(config, "clock24", clock24) || clock24.empty() || clock24.front() != '0' ||
                clock24.find_first_not_of(" \t\r\n", 1) != std::string::npos;
    if (g.clock24) return;

    // Keep the process time locale for the date, which i18n translates separately.
    const locale_t locale = newlocale(LC_TIME_MASK, "", nullptr);
    if (!locale) return;
    for (size_t i = 0; i < g.clockPeriods.size(); ++i) {
        const char* period = nl_langinfo_l(i == 0 ? AM_STR : PM_STR, locale);
        if (period && *period && g_utf8_validate(period, -1, nullptr)) g.clockPeriods[i] = period;
    }
    freelocale(locale);
}

int main(int argc, char** argv) {
    g_t0 = std::chrono::steady_clock::now();
    if (const char* t = std::getenv("END4PC_LOCK_TIMING")) g_timing = *t && std::strcmp(t, "0") != 0;
    if (const char* n = std::getenv("END4PC_LOCK_NOCACHE")) g_pixCache = !*n || std::strcmp(n, "0") == 0;
    markBeforeMain();
    if (pipe(g_wake) != 0) return 1;
    loadColors();
    applyShellFont(fontFamilyFromConfig(configFile()), fontWeightFromConfig(configFile(), "fontWeightText", 450), fontWeightFromConfig(configFile(), "fontWeightTitle", 550));
    const std::string config = readFile(configFile());
    {
        std::string lang;
        jsonValue(config, "language", lang);
        const std::string cd = configFile().substr(0, configFile().rfind('/'));
        i18n::load(lang, assetsDir(), cd + "/i18n");
    }
    readClockConfig(config);
    mark("config, colors, font, language");
    loadProfile();
    mark("profile and avatar");
    loadWeatherCache();
    readAnimConfig();
    std::string screens;
    jsonValue(config, "lockScreens", screens);
    std::istringstream monitorList(screens);
    for (std::string name; std::getline(monitorList, name, ',');)
        if (!name.empty()) g.lockScreens.push_back(name);
    if (argc >= 2 && std::strcmp(argv[1], "--warm") == 0) { warmBackgrounds(); joinPixWriters(); return 0; }
    if (argc >= 3 && std::strcmp(argv[1], "--dump-frames") == 0) { dumpFrames(argv[2]); joinPixWriters(); return 0; }
    if (argc >= 3 && std::strcmp(argv[1], "--dump-player") == 0) { dumpPlayer(argv[2]); joinPixWriters(); return 0; }

    g.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    g.display = wl_display_connect(nullptr);
    if (!g.display) { std::fprintf(stderr, "end4pC-lock: no Wayland display\n"); return 1; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    wl_display_roundtrip(g.display);
    wl_display_roundtrip(g.display);
    if (!g.manager || !g.compositor || !g.shm) { std::fprintf(stderr, "end4pC-lock: the compositor lacks ext-session-lock-v1\n"); return 1; }
    mark("Wayland connected");

    g.lock = ext_session_lock_manager_v1_lock(g.manager);
    ext_session_lock_v1_add_listener(g.lock, &kLockListener, nullptr);
    for (auto& o : g.outputs) createLockSurface(*o);
    wl_display_roundtrip(g.display);
    if (g.finished) { std::fprintf(stderr, "end4pC-lock: another lock screen is already running\n"); return 1; }
    mark("lock asked for, first events handled");

    std::thread(weatherLoop).detach();
    startFingerprint();
    startPlayer();

    int lastMinute = -1;
    while (!g.done && !g.finished) {
        wl_display_flush(g.display);
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        std::vector<pollfd> fds = {{wl_display_get_fd(g.display), POLLIN, 0}, {g_wake[0], POLLIN, 0}};
        for (const auto& [key, f] : g_feeds) if (f.fd >= 0) fds.push_back({f.fd, POLLIN, 0});
        const bool playerMoving = playerPlaying() || playerSliding();
        if (poll(fds.data(), fds.size(), g.animPhase != App::AnimIdle ? 30 : playerMoving ? 66 : 1000) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);

        bool redraw = playerMoving && !g.unlocking;  // (the line of the song moves)
        if (!g_feeds.empty() && readFeeds()) {
            redraw = true;
            for (auto& o : g.outputs) {
                o->cacheDirty = true;
                o->wallpaperDrawn = false;
            }
        }
        if (fds[1].revents & POLLIN) {
            char b[16];
            const ssize_t n = read(g_wake[0], b, sizeof b);
            for (ssize_t i = 0; i < n; i++) {
                if (b[i] == 'w') redraw = true;
                if (b[i] == 'a') {
                    if (g.authResult == 1 || g.authResult == 2) { startUnlock(); break; }
                    g.status = App::Wrong;
                    redraw = true;
                }
            }
        }
        if (g.done) break;
        animTick(redraw);
        if (g.done) break;
        const time_t now = time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        if (lt.tm_min != lastMinute) { lastMinute = lt.tm_min; redraw = true; }
        if (redraw) drawAll();
#ifdef LOCK_DEBUG
        if (g.locked && std::chrono::steady_clock::now() - g.lockedAt > std::chrono::seconds(LOCK_DEBUG)) unlockAndExit();
#endif
    }
    if (g.finished && g.lock) std::fprintf(stderr, "end4pC-lock: the compositor ended the lock\n");
    std::fflush(nullptr);
    unlink(lockMarker().c_str());
    _exit(g.done ? 0 : 1);
}
