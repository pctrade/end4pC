// What Fil3s knows about folders: the listing (read in a thread, so a big or slow folder never freezes the window), the kind of every file,
// the thumbnails (decoded in worker threads), the trash and the file operations. Included once by fil3s.cpp, inside its anonymous namespace.
#pragma once

namespace fs = std::filesystem;

enum class Kind { Folder, Image, Audio, Video, Archive, Pdf, Text, Code, Other };

struct Entry {
    std::string name, path;
    bool dir = false, link = false, hidden = false, regular = false, executable = false;  // regular: a plain file (not a pipe, a device or a socket)
    uint64_t size = 0;
    time_t mtime = 0, atime = 0;
    Kind kind = Kind::Other;
    std::string extension;
    std::string lower;  // the name in lower case, for sorting and filtering
};

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string extensionOf(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? "" : lowerCase(name.substr(dot + 1));
}
Kind kindOf(const std::string& name, bool dir) {
    if (dir) return Kind::Folder;
    static const std::map<std::string, Kind> table = [] {
        std::map<std::string, Kind> t;
        for (const char* e : {"png", "jpg", "jpeg", "gif", "webp", "bmp", "svg", "tif", "tiff", "avif", "heic", "ico", "jxl"}) t[e] = Kind::Image;
        for (const char* e : {"mp3", "flac", "ogg", "opus", "wav", "m4a", "aac", "wma", "aiff", "mka"}) t[e] = Kind::Audio;
        for (const char* e : {"mp4", "mkv", "webm", "avi", "mov", "wmv", "flv", "m4v", "mpg", "mpeg"}) t[e] = Kind::Video;
        for (const char* e : {"zip", "tar", "gz", "xz", "bz2", "zst", "7z", "rar", "tgz", "iso", "lz4", "deb", "rpm", "pkg"}) t[e] = Kind::Archive;
        t["pdf"] = Kind::Pdf;
        for (const char* e : {"txt", "md", "log", "csv", "json", "yaml", "yml", "toml", "ini", "conf", "cfg", "xml", "rtf", "odt", "doc", "docx"}) t[e] = Kind::Text;
        for (const char* e : {"c", "h", "cpp", "hpp", "cc", "inc", "rs", "go", "py", "js", "ts", "qml", "lua", "sh", "fish", "java", "kt", "html", "css", "kdl", "build", "nix"}) t[e] = Kind::Code;
        return t;
    }();
    const auto it = table.find(extensionOf(name));
    return it == table.end() ? Kind::Other : it->second;
}
const char* iconFor(Kind k) {
    switch (k) {
        case Kind::Folder: return "folder";
        case Kind::Image: return "gallery";
        case Kind::Audio: return "music-note";
        case Kind::Video: return "videocamera";
        case Kind::Archive: return "zip-file";
        case Kind::Pdf: return "document-text";
        case Kind::Text: return "file-text";
        case Kind::Code: return "code-file";
        default: return "file";
    }
}
// The colour pair of the tile of a kind (so a folder of mixed files reads at a glance): [container, on-container].
std::pair<const char*, const char*> tileColors(Kind k) {
    switch (k) {
        case Kind::Folder: return {"primary_container", "on_primary_container"};
        case Kind::Image: case Kind::Video: return {"tertiary_container", "on_tertiary_container"};
        case Kind::Audio: case Kind::Archive: return {"secondary_container", "on_secondary_container"};
        default: return {"surface_container_highest", "on_surface_variant"};
    }
}

std::string formatSize(uint64_t b) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(b);
    int u = 0;
    while (v >= 1024 && u < 4) { v /= 1024; u++; }
    char buf[32];
    if (u == 0) std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(b));
    else std::snprintf(buf, sizeof buf, v < 10 ? "%.1f %s" : "%.0f %s", v, units[u]);
    return buf;
}
std::string formatDate(time_t t) {
    if (t == 0) return "";
    struct tm tmv{};
    localtime_r(&t, &tmv);
    const time_t now = time(nullptr);
    struct tm nowTm{};
    localtime_r(&now, &nowTm);
    char buf[32];
    std::strftime(buf, sizeof buf, tmv.tm_year == nowTm.tm_year ? "%d %b %H:%M" : "%d %b %Y", &tmv);
    return buf;
}

// ---- preferences -----------------------------------------------------------------------------------------------------------------------
std::string hiddenPreferenceFile() { return dataDir() + "/fil3s-show-hidden"; }

bool rememberHidden(bool show) {
    std::error_code ec;
    fs::create_directories(dataDir(), ec);
    if (ec) return false;
    const std::string path = hiddenPreferenceFile();
    std::string tmp = path + ".XXXXXX";
    const int fd = mkostemp(tmp.data(), O_CLOEXEC);
    if (fd < 0) return false;
    const char value = show ? '1' : '0';
    const bool written = write(fd, &value, 1) == 1;
    const bool closed = close(fd) == 0;
    if (written && closed && rename(tmp.c_str(), path.c_str()) == 0) return true;
    unlink(tmp.c_str());
    return false;
}

// ---- places ----------------------------------------------------------------------------------------------------------------------------
std::string trashFiles() { return envDir("XDG_DATA_HOME", homeDir() + "/.local/share") + "/Trash/files"; }
std::string trashInfo() { return envDir("XDG_DATA_HOME", homeDir() + "/.local/share") + "/Trash/info"; }

struct Place { std::string label, path, icon; int user = -1; };  // user: the position among the places the user can edit (-1: a fixed row)

// A place from a folder: the usual ones (Home, Documents...) keep their name and icon, any other is called after the folder.
Place makePlace(const std::string& path) {
    const std::string home = homeDir();
    struct Known { const char* dir; const char* label; const char* icon; };
    static const Known known[] = {{"", "Home", "home-smile"}, {"/Desktop", "Desktop", "monitor"}, {"/Documents", "Documents", "document-text"}, {"/Downloads", "Downloads", "download-minimalistic"},
                                  {"/Music", "Music", "music-note"}, {"/Pictures", "Pictures", "gallery"}, {"/Videos", "Videos", "videocamera"}};
    for (const Known& k : known) if (path == home + k.dir) return {i18n::tr(k.label), path, k.icon, -1};
    const std::string name = fs::path(path).filename().string();
    return {name.empty() ? path : name, path, "folder", -1};
}
std::string placesFile() { return dataDir() + "/fil3s-places.txt"; }
std::vector<std::string> defaultPlacePaths() {
    std::vector<std::string> out;
    const std::string home = homeDir();
    out.push_back(home);
    for (const char* d : {"/Desktop", "/Documents", "/Downloads", "/Music", "/Pictures", "/Videos"}) {
        std::error_code ec;
        if (fs::is_directory(home + d, ec)) out.push_back(home + d);
    }
    return out;
}
// The places of the sidebar: the list the user made (one folder per line), or the usual ones until they change it.
std::vector<Place> loadPlaces() {
    std::vector<std::string> paths;
    std::ifstream f(placesFile());
    std::string line;
    bool have = false;
    while (std::getline(f, line)) {
        have = true;
        if (!line.empty() && line[0] == '/' && line.find('\0') == std::string::npos) paths.push_back(line);
    }
    if (!have) paths = defaultPlacePaths();
    std::vector<Place> out;
    for (const std::string& p : paths) { Place pl = makePlace(p); pl.user = static_cast<int>(out.size()); out.push_back(std::move(pl)); }
    return out;
}
void savePlaces(const std::vector<Place>& places) {
    std::error_code ec;
    fs::create_directories(dataDir(), ec);
    const std::string tmp = placesFile() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        for (const Place& p : places) f << p.path << "\n";
    }
    fs::rename(tmp, placesFile(), ec);
}
void resetPlaces() { std::error_code ec; fs::remove(placesFile(), ec); }
// Mounted drives: what the desktop mounts under /run/media/<user> or /mnt.
std::vector<Place> driveList() {
    std::vector<Place> out;
    const char* user = std::getenv("USER");
    for (const std::string& base : {std::string("/run/media/") + (user ? user : ""), std::string("/mnt")}) {
        std::error_code ec;
        for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2)) out.push_back({it->path().filename().string(), it->path().string(), "usb"});
        }
    }
    return out;
}

// ---- the listing -----------------------------------------------------------------------------------------------------------------------
enum class Sort { Name, Size, Date, Type, Extension, Accessed };

struct Listing {
    std::string dir;
    std::vector<Entry> entries;
    bool done = false;
    std::string error;
    uint64_t generation = 0;
};

std::vector<Entry> readFolder(const std::string& dir, std::string& error) {
    std::vector<Entry> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        Entry e;
        e.name = it->path().filename().string();
        e.path = it->path().string();
        e.hidden = !e.name.empty() && e.name[0] == '.';
        struct stat st{}, lst{};
        if (lstat(e.path.c_str(), &lst) != 0) continue;
        e.link = S_ISLNK(lst.st_mode);
        if (e.link) { if (stat(e.path.c_str(), &st) != 0) st = lst; } else st = lst;
        e.dir = S_ISDIR(st.st_mode);
        e.regular = S_ISREG(st.st_mode);
        e.size = e.dir ? 0 : static_cast<uint64_t>(st.st_size);
        e.mtime = st.st_mtime;
        e.atime = st.st_atime;
        e.executable = e.regular && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) && access(e.path.c_str(), X_OK) == 0;
        e.extension = extensionOf(e.name);
        e.kind = kindOf(e.name, e.dir);
        e.lower = lowerCase(e.name);
        out.push_back(std::move(e));
    }
    if (ec) error = ec.message();
    return out;
}
void sortEntries(std::vector<Entry>& v, Sort by, bool descending, bool foldersFirst, bool hiddenLast) {
    std::stable_sort(v.begin(), v.end(), [&](const Entry& a, const Entry& b) {
        if (foldersFirst && a.dir != b.dir) return a.dir;
        if (hiddenLast && a.hidden != b.hidden) return !a.hidden;
        int c = 0;
        if (by == Sort::Size) c = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
        else if (by == Sort::Date) c = (a.mtime > b.mtime) - (a.mtime < b.mtime);
        else if (by == Sort::Accessed) c = (a.atime > b.atime) - (a.atime < b.atime);
        else if (by == Sort::Type) c = (a.kind > b.kind) - (a.kind < b.kind);
        else if (by == Sort::Extension) c = a.extension.compare(b.extension);
        if (c == 0) {
            // natural order: "file2" before "file10"
            const std::string &x = a.lower, &y = b.lower;
            size_t i = 0, j = 0;
            while (i < x.size() && j < y.size() && c == 0) {
                if (std::isdigit(static_cast<unsigned char>(x[i])) && std::isdigit(static_cast<unsigned char>(y[j]))) {
                    size_t i2 = i, j2 = j;
                    while (i2 < x.size() && std::isdigit(static_cast<unsigned char>(x[i2]))) i2++;
                    while (j2 < y.size() && std::isdigit(static_cast<unsigned char>(y[j2]))) j2++;
                    const unsigned long long nx = std::strtoull(x.substr(i, i2 - i).c_str(), nullptr, 10), ny = std::strtoull(y.substr(j, j2 - j).c_str(), nullptr, 10);
                    c = nx < ny ? -1 : nx > ny ? 1 : 0;
                    i = i2; j = j2;
                } else { c = x[i] < y[j] ? -1 : x[i] > y[j] ? 1 : 0; i++; j++; }
            }
            if (c == 0) c = x.size() < y.size() ? -1 : x.size() > y.size() ? 1 : 0;
        }
        return descending ? c > 0 : c < 0;
    });
}

// A folder is read in a thread: the window shows "loading" and then the files, whatever the size of the folder.
struct Lister : std::enable_shared_from_this<Lister> {
    std::mutex mutex;
    Listing result;
    std::atomic<uint64_t> wanted{0};
    int wakeFd = -1;
    void request(const std::string& dir, uint64_t generation) {
        wanted = generation;
        std::thread([this, keepAlive = shared_from_this(), dir, generation] {
            Listing l;
            l.dir = dir;
            l.generation = generation;
            l.entries = readFolder(dir, l.error);
            l.done = true;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (wanted.load() != generation) return;
                result = std::move(l);
            }
            const char c = 1;
            if (wakeFd >= 0 && write(wakeFd, &c, 1) < 0) {}
        }).detach();
    }
    bool take(Listing& out) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!result.done) return false;
        out = std::move(result);
        result = Listing{};
        return true;
    }
};

// Selection totals and free space can both touch slow or removable drives.
struct StatusRequest {
    uint64_t serial = 0;
    std::string path;
    std::vector<std::string> folders;
    uint64_t bytes = 0;
    bool measure = false;
};
struct StatusResult {
    uint64_t serial = 0;
    fs::space_info space{};
    uint64_t bytes = 0;
    bool measured = false, partial = false;
};
struct StatusProbe : std::enable_shared_from_this<StatusProbe> {
    std::mutex mutex;
    StatusRequest pending;
    StatusResult result;
    std::atomic<uint64_t> wanted{0};
    bool running = false;
    int wakeFd = -1;

    void request(StatusRequest next) {
        std::lock_guard<std::mutex> lock(mutex);
        wanted = next.serial;
        pending = std::move(next);
        if (running) return;
        running = true;
        std::thread([self = shared_from_this()] { self->run(); }).detach();
    }

    void run() {
        for (;;) {
            StatusRequest job;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (pending.serial == 0) { running = false; return; }
                job = std::exchange(pending, {});
            }
            StatusResult next;
            next.serial = job.serial;
            next.bytes = job.bytes;
            next.measured = job.measure;
            std::error_code ec;
            next.space = fs::space(job.path, ec);
            if (ec) next.space = {};
            auto folders = std::move(job.folders);
            while (!folders.empty() && wanted == job.serial) {
                auto folder = std::move(folders.back());
                folders.pop_back();
                const auto folderType = fs::symlink_status(folder, ec);
                if (ec || !fs::is_directory(folderType)) { next.partial = true; continue; }
                fs::directory_iterator it(folder, ec), end;
                if (ec) { next.partial = true; continue; }
                while (it != end && wanted == job.serial) {
                    const auto type = it->symlink_status(ec);
                    if (!ec && fs::is_directory(type)) folders.push_back(it->path().string());
                    else if (!ec && fs::is_regular_file(type)) {
                        const auto size = it->file_size(ec);
                        if (!ec) next.bytes += size;
                    }
                    if (ec) next.partial = true;
                    it.increment(ec);
                    if (ec) next.partial = true;
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (wanted != job.serial) continue;
                result = next;
            }
            const char c = 1;
            if (wakeFd >= 0 && write(wakeFd, &c, 1) < 0) {}
        }
    }

    StatusResult take() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(result, {});
    }
};

std::string entryType(const Entry& entry) {
    if (entry.link) return i18n::tr("Symbolic link");
    if (entry.dir) return i18n::tr("Folder");
    if (!entry.regular) return i18n::tr("Special file");
    gboolean uncertain = false;
    char* contentType = g_content_type_guess(entry.name.c_str(), nullptr, 0, &uncertain);
    char* description = !uncertain && contentType ? g_content_type_get_description(contentType) : nullptr;
    std::string label;
    if (description) label = description;
    else if (!entry.extension.empty()) {
        label = entry.extension;
        for (char& c : label) c = g_ascii_toupper(c);
        label += " · ";
        label += i18n::tr("File");
    } else label = i18n::tr("File");
    g_free(description);
    g_free(contentType);
    return label;
}

// ---- thumbnails ------------------------------------------------------------------------------------------------------------------------
constexpr int kThumbPx = 256;      // the size of the thumbnails on disk ("large" of the freedesktop spec)
constexpr int kMemoryPx = 192;     // what is kept in memory: the biggest tile (96 px) at a scale of 2
constexpr size_t kMemoryMax = 400; // thumbnails kept in memory (about 60 MB at the most); the disk cache has the rest
struct Thumb { cairo_surface_t* surface = nullptr; bool failed = false; uint64_t used = 0; };

cairo_surface_t* surfaceFromPixbuf(GdkPixbuf* pb) {
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    unsigned char* dst = cairo_image_surface_get_data(img);
    const int drs = cairo_image_surface_get_stride(img);
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            const guchar* q = px + y * rs + x * nch;
            const unsigned a = nch == 4 ? q[3] : 255;
            *reinterpret_cast<uint32_t*>(dst + y * drs + x * 4) = (a << 24) | (((q[0] * a + 127) / 255) << 16) | (((q[1] * a + 127) / 255) << 8) | ((q[2] * a + 127) / 255);
        }
    cairo_surface_mark_dirty(img);
    return img;
}

// The thumbnail cache of the desktop (freedesktop.org thumbnail spec): ~/.cache/thumbnails/large/<md5 of the file:// uri>.png, with the modification
// time of the picture inside the PNG. Fil3s reads what other file managers made and writes what they can read, so a folder seen once is instant
// the next time (and in the next session).
struct ThumbKey { std::string uri, name; };
ThumbKey thumbKey(const std::string& path) {
    ThumbKey k;
    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    if (!uri) return k;
    gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_MD5, uri, -1);
    k.uri = uri;
    k.name = std::string(sum) + ".png";
    g_free(uri);
    g_free(sum);
    return k;
}
std::string thumbRoot() { return envDir("XDG_CACHE_HOME", homeDir() + "/.cache") + "/thumbnails"; }
// A cached thumbnail whose recorded mtime is the file's own (a changed picture is made again), or nullptr.
GdkPixbuf* readCachedThumb(const ThumbKey& key, time_t mtime) {
    if (key.name.empty()) return nullptr;
    for (const char* size : {"large", "normal"}) {
        const std::string file = thumbRoot() + "/" + size + "/" + key.name;
        if (access(file.c_str(), R_OK) != 0) continue;
        GdkPixbuf* pb = gdk_pixbuf_new_from_file(file.c_str(), nullptr);
        if (!pb) continue;
        const char* recorded = gdk_pixbuf_get_option(pb, "tEXt::Thumb::MTime");
        if (recorded && std::atoll(recorded) == static_cast<long long>(mtime)) return pb;
        g_object_unref(pb);
    }
    return nullptr;
}
void writeCachedThumb(GdkPixbuf* pb, const ThumbKey& key, time_t mtime, const std::string& sourcePath) {
    if (key.name.empty() || sourcePath.rfind(thumbRoot() + "/", 0) == 0) return;  // (never a thumbnail of a thumbnail)
    const std::string dir = thumbRoot() + "/large";
    std::error_code ec;
    fs::create_directories(dir, ec);
    chmod(thumbRoot().c_str(), 0700);
    chmod(dir.c_str(), 0700);
    const std::string tmp = dir + "/." + key.name + "." + std::to_string(getpid()) + ".tmp", target = dir + "/" + key.name;
    const std::string mt = std::to_string(static_cast<long long>(mtime));
    if (gdk_pixbuf_save(pb, tmp.c_str(), "png", nullptr, "tEXt::Thumb::URI", key.uri.c_str(), "tEXt::Thumb::MTime", mt.c_str(), "tEXt::Software", "end4pC Fil3s", nullptr)) {
        chmod(tmp.c_str(), 0600);
        if (rename(tmp.c_str(), target.c_str()) != 0) unlink(tmp.c_str());
    } else unlink(tmp.c_str());
}

// Decodes the thumbnails of the files on screen, the last asked first (what you scrolled to is what you want to see). Two threads.
struct Thumbnailer {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> pending;  // a stack: the back is the next one
    std::set<std::string> queued;
    std::map<std::string, Thumb> done;  // written by the threads under the mutex, read by the window under it too
    std::vector<std::thread> threads;
    std::atomic<bool> stop{false};
    int wakeFd = -1;
    uint64_t clock = 0;

    void start(int wake) {
        wakeFd = wake;
        for (int i = 0; i < 2; i++) threads.emplace_back([this] { work(); });
    }
    void shutdown() {
        stop = true;
        cv.notify_all();
        for (auto& t : threads) t.join();
        for (auto& [p, th] : done) if (th.surface) cairo_surface_destroy(th.surface);
    }
    // The thumbnail of a file when it is ready (nullptr until then, or when it cannot be decoded); asking for it queues the work.
    cairo_surface_t* get(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = done.find(path);
        if (it != done.end()) { it->second.used = ++clock; return it->second.surface; }
        if (queued.insert(path).second) { pending.push_back(path); cv.notify_one(); }
        else {  // asked again: it is on screen now, so it moves to the front of the line
            auto p = std::find(pending.begin(), pending.end(), path);
            if (p != pending.end() && p + 1 != pending.end()) { pending.erase(p); pending.push_back(path); }
        }
        return nullptr;
    }
    // The oldest quarter goes when there are too many (what is on screen is the most recently asked for, so it stays). Call with the mutex held.
    void evictLocked() {
        if (done.size() <= kMemoryMax) return;
        std::vector<std::pair<uint64_t, std::string>> order;
        for (auto& [p, th] : done) order.push_back({th.used, p});
        std::sort(order.begin(), order.end());
        for (size_t i = 0; i < order.size() / 4; i++) {
            auto it = done.find(order[i].second);
            if (it->second.surface) cairo_surface_destroy(it->second.surface);
            queued.erase(it->first);
            done.erase(it);
        }
    }
    void forgetPending() {  // another folder: what was queued for the old one is not needed
        std::lock_guard<std::mutex> lock(mutex);
        pending.clear();
        queued.clear();
        for (auto& [p, th] : done) queued.insert(p);
        // keep the decoded ones (cheap to reuse when going back) but bound them
        evictLocked();
    }
    void work() {
        while (true) {
            std::string path;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return stop.load() || !pending.empty(); });
                if (stop) return;
                path = pending.back();
                pending.pop_back();
            }
            Thumb th;
            struct stat st{};
            GdkPixbuf* pb = nullptr;
            if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                const ThumbKey key = thumbKey(path);
                pb = readCachedThumb(key, st.st_mtime);  // made before (by us or by another file manager): no decoding of the picture itself
                if (!pb) {
                    // Pictures come from anywhere: only regular files, only the formats gdk-pixbuf knows, nothing with a huge canvas (a decompression bomb).
                    int iw = 0, ih = 0;
                    if (gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) && iw > 0 && ih > 0 && static_cast<int64_t>(iw) * ih <= 120'000'000)
                        pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), kThumbPx, kThumbPx, TRUE, nullptr);
                    if (pb) {
                        GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(pb);
                        if (oriented) { g_object_unref(pb); pb = oriented; }
                        writeCachedThumb(pb, key, st.st_mtime, path);
                    }
                }
            }
            if (pb) {
                const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), big = std::max(w, h);
                if (big > kMemoryPx) {  // the memory copy is only as big as it is ever drawn
                    GdkPixbuf* small = gdk_pixbuf_scale_simple(pb, std::max(1, w * kMemoryPx / big), std::max(1, h * kMemoryPx / big), GDK_INTERP_BILINEAR);
                    if (small) { g_object_unref(pb); pb = small; }
                }
                th.surface = surfaceFromPixbuf(pb);
                g_object_unref(pb);
            } else th.failed = true;
            {
                std::lock_guard<std::mutex> lock(mutex);
                th.used = ++clock;
                done[path] = th;
                evictLocked();
            }
            const char c = 1;
            if (wakeFd >= 0 && write(wakeFd, &c, 1) < 0) {}
        }
    }
};

// ---- file operations -------------------------------------------------------------------------------------------------------------------
// The name a copy gets when the destination already has that one: "file (2).txt".
std::string freeName(const std::string& dir, const std::string& name) {
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(fs::path(dir) / name, ec))) return (fs::path(dir) / name).string();
    const size_t dot = name.rfind('.');
    const bool hasExt = dot != std::string::npos && dot != 0 && name.find('.') != std::string::npos && !fs::is_directory(fs::path(dir) / name, ec);
    const std::string stem = hasExt ? name.substr(0, dot) : name, ext = hasExt ? name.substr(dot) : "";
    for (int i = 2; i < 10000; i++) {
        const std::string candidate = stem + " (" + std::to_string(i) + ")" + ext;
        if (!fs::exists(fs::symlink_status(fs::path(dir) / candidate, ec))) return (fs::path(dir) / candidate).string();
    }
    return (fs::path(dir) / name).string();
}
std::string urlEncodePath(const std::string& p) {
    std::string out;
    for (const unsigned char c : p) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); out += b; }
    }
    return out;
}
std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    return out;
}

bool copyAny(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    if (fs::is_directory(fs::symlink_status(from, ec))) {
        const fs::path fromAbs = fs::weakly_canonical(from, ec), toAbs = fs::weakly_canonical(to, ec);
        if (toAbs.string().rfind(fromAbs.string() + "/", 0) == 0) { error = "cannot copy a folder into itself"; return false; }
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    } else fs::copy(from, to, fs::copy_options::copy_symlinks, ec);
    if (ec) { error = ec.message(); return false; }
    return true;
}
bool moveAny(const fs::path& from, const fs::path& to, std::string& error) {
    // RENAME_NOREPLACE: never overwrite what is already there, even if it appeared after the name was chosen.
    if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) return true;
    std::error_code ec(errno, std::generic_category());
    if (ec != std::errc::cross_device_link) { error = ec.message(); return false; }
    if (!copyAny(from, to, error)) return false;
    fs::remove_all(from, ec);
    if (ec) { error = ec.message(); return false; }
    return true;
}
bool trashOne(const std::string& path, std::string& error) {
    std::error_code ec;
    fs::create_directories(trashFiles(), ec);
    fs::create_directories(trashInfo(), ec);
    const std::string name = fs::path(path).filename().string();
    const std::string dest = freeName(trashFiles(), name);
    const std::string stored = fs::path(dest).filename().string();
    {
        std::ofstream info(trashInfo() + "/" + stored + ".trashinfo");
        char date[32];
        const time_t now = time(nullptr);
        struct tm tmv{};
        localtime_r(&now, &tmv);
        std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tmv);
        info << "[Trash Info]\nPath=" << urlEncodePath(path) << "\nDeletionDate=" << date << "\n";
    }
    if (!moveAny(path, dest, error)) { fs::remove(trashInfo() + "/" + stored + ".trashinfo", ec); return false; }
    return true;
}
// Back to where it was deleted from (read from the .trashinfo); false when that cannot be known.
bool restoreOne(const std::string& storedPath, std::string& error) {
    const std::string stored = fs::path(storedPath).filename().string();
    std::ifstream f(trashInfo() + "/" + stored + ".trashinfo");
    std::string line, original;
    while (std::getline(f, line)) if (line.rfind("Path=", 0) == 0) original = urlDecode(line.substr(5));
    if (original.empty() || original[0] != '/' || original.find('\0') != std::string::npos) { error = "no record of where it came from"; return false; }
    std::error_code ec;
    fs::create_directories(fs::path(original).parent_path(), ec);
    const std::string dest = freeName(fs::path(original).parent_path().string(), fs::path(original).filename().string());
    if (!moveAny(storedPath, dest, error)) return false;
    fs::remove(trashInfo() + "/" + stored + ".trashinfo", ec);
    return true;
}
bool deleteForever(const std::string& path, std::string& error) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) { error = ec.message(); return false; }
    std::error_code e2;
    if (fs::path(path).parent_path() == trashFiles()) fs::remove(trashInfo() + "/" + fs::path(path).filename().string() + ".trashinfo", e2);
    return true;
}

// The duration of a media file, asked to ffprobe directly (no shell: the name of the file is never parsed by anything but ffprobe).
double probeDuration(const std::string& path) {
    int out[2];
    if (pipe(out) != 0) return 0;
    const pid_t pid = fork();
    if (pid < 0) { close(out[0]); close(out[1]); return 0; }
    if (pid == 0) {
        dup2(out[1], 1);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 2); }
        close(out[0]);
        close(out[1]);
        execlp("ffprobe", "ffprobe", "-v", "error", "-protocol_whitelist", "file", "-show_entries", "format=duration", "-of", "csv=p=0", path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(out[1]);
    char buf[64] = {0};
    const ssize_t n = read(out[0], buf, sizeof buf - 1);
    close(out[0]);
    int status;
    waitpid(pid, &status, 0);
    return n > 0 ? std::atof(buf) : 0;
}
bool haveTool(const std::string& name) {
    if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    std::stringstream ss(path ? path : "/usr/bin");
    std::string dir;
    while (std::getline(ss, dir, ':')) if (!dir.empty() && access((dir + "/" + name).c_str(), X_OK) == 0) return true;
    return false;
}

// A program to run for a conversion, an archive or an extraction (ffmpeg, magick, zip, bsdtar...), with what the window needs to show it.
struct Cmd {
    std::vector<std::string> argv;
    std::string label;     // "photo.png"
    std::string output;    // the file or folder it makes: removed when the command fails or is cancelled
    std::string cwd;
    double duration = 0;   // seconds of media (ffmpeg): lets the progress bar be real instead of just moving
    std::string probe;     // a media file to ask ffprobe the duration of, when the command starts (so building the job never waits)
};

// One worker thread does the operations one after the other, with a line of status for the window.
enum class Op { Copy, Duplicate, Move, Trash, Restore, Delete, Command, CopyText };
struct Job { Op op; std::vector<std::string> paths; std::string dest; std::vector<Cmd> cmds; };
struct Worker {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::string failure, label;
    bool textCopied = false;
    std::vector<std::string> outputs;  // what the commands made (the window selects it when it is ready)
    double fraction = -1;              // 0..1 of the command running; -1: it cannot tell (the bar just moves)
    int done = 0, total = 0;
    bool busy = false, changed = false, stop = false, running = false;
    std::atomic<bool> cancel{false};
    std::atomic<pid_t> child{0};
    std::thread thread;
    int wakeFd = -1;

    void start(int wake) { wakeFd = wake; thread = std::thread([this] { run(); }); }
    void shutdown() {
        cancel = true;
        if (pid_t c = child.load()) kill(c, SIGTERM);
        { std::lock_guard<std::mutex> lock(mutex); stop = true; }
        cv.notify_all();
        if (thread.joinable()) thread.join();
    }
    void submit(Job j) {
        { std::lock_guard<std::mutex> lock(mutex); jobs.push_back(std::move(j)); busy = true; cancel = false; changed = true; }
        cv.notify_one();
    }
    void cancelAll() {
        cancel = true;
        { std::lock_guard<std::mutex> lock(mutex); jobs.clear(); }
        if (pid_t c = child.load()) kill(c, SIGTERM);
    }
    void wake() { const char c = 1; if (wakeFd >= 0 && write(wakeFd, &c, 1) < 0) {} }

    // Runs one command; the progress of ffmpeg comes through `-progress pipe:1` (out_time_us=...).
    bool runCommand(const Cmd& cmd, std::string& error, int input = -1) {
        int pipefd[2];
        if (pipe2(pipefd, O_CLOEXEC) != 0) { error = "pipe failed"; return false; }
        const pid_t pid = fork();
        if (pid < 0) { close(pipefd[0]); close(pipefd[1]); error = "fork failed"; return false; }
        if (pid == 0) {
            if (!cmd.cwd.empty() && chdir(cmd.cwd.c_str()) != 0) _exit(126);
            const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
            // wl-copy keeps a background process alive to serve the selection.
            // It must not hold the pipe that the worker reads for command output.
            const int output = input >= 0 ? null : pipefd[1];
            if (output >= 0) { dup2(output, 1); dup2(output, 2); }
            close(pipefd[0]);
            close(pipefd[1]);
            const int source = input >= 0 ? input : null;
            if (source >= 0) dup2(source, 0);
            std::vector<char*> args;
            for (const std::string& a : cmd.argv) args.push_back(const_cast<char*>(a.c_str()));
            args.push_back(nullptr);
            execvp(args[0], args.data());
            _exit(127);
        }
        close(pipefd[1]);
        child = pid;
        std::string buffer, lastLine;
        char chunk[1024];
        ssize_t n;
        while ((n = read(pipefd[0], chunk, sizeof chunk)) > 0) {
            buffer.append(chunk, static_cast<size_t>(n));
            size_t nl;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                const std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                if (line.rfind("out_time_us=", 0) == 0 && cmd.duration > 0) {
                    const double us = std::atof(line.c_str() + 12);
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        fraction = std::clamp(us / 1e6 / cmd.duration, 0.0, 1.0);
                        changed = true;
                    }
                    wake();
                } else if (!line.empty() && line.find('=') == std::string::npos) lastLine = line;
            }
        }
        close(pipefd[0]);
        int status = 0;
        pid_t waited;
        do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
        child = 0;
        const bool ok = waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 && !cancel.load();
        if (!ok) error = cancel.load() ? "cancelled" : WIFEXITED(status) && WEXITSTATUS(status) == 127 ? cmd.argv[0] + " is not installed" : lastLine.empty() ? "failed" : lastLine;
        return ok;
    }

    bool copyText(const std::string& text) {
        // Use stdin so even a large selection is independent of the OS argument length limit.
        const int fd = memfd_create("fil3s-clipboard", MFD_CLOEXEC);
        if (fd < 0) return false;
        size_t done = 0;
        while (done < text.size()) {
            const ssize_t n = write(fd, text.data() + done, text.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            done += static_cast<size_t>(n);
        }
        std::string error;
        const bool ok = done == text.size() && lseek(fd, 0, SEEK_SET) == 0 &&
                        runCommand({{"wl-copy", "--type", "text/plain;charset=utf-8"}, "", "", "", 0, ""}, error, fd);
        close(fd);
        return ok;
    }

    void run() {
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return stop || !jobs.empty(); });
                if (stop) return;
                job = std::move(jobs.front());
                jobs.pop_front();
                running = true;
                done = 0;
                total = static_cast<int>(job.op == Op::Command ? job.cmds.size() : job.paths.size());
                fraction = -1;
                changed = true;
            }
            wake();
            if (job.op == Op::CopyText) {
                const bool ok = !cancel && copyText(job.dest);
                std::lock_guard<std::mutex> lock(mutex);
                textCopied |= ok;
                if (!ok && !cancel) failure = i18n::tr("Could not copy to the clipboard");
            } else if (job.op == Op::Command) {
                for (const Cmd& cmd : job.cmds) {
                    if (cancel) break;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        label = cmd.label;
                        fraction = -1;
                        changed = true;
                    }
                    wake();
                    std::string error;
                    Cmd run = cmd;
                    if (!cmd.probe.empty()) run.duration = probeDuration(cmd.probe);
                    const bool ok = runCommand(run, error);
                    std::error_code ec;
                    if (!ok && !cmd.output.empty()) fs::remove_all(cmd.output, ec);
                    std::lock_guard<std::mutex> lock(mutex);
                    done++;
                    if (ok && !cmd.output.empty()) outputs.push_back(cmd.output);
                    else if (!ok && error != "cancelled") failure = cmd.label + ": " + error;
                    changed = true;
                }
            } else for (const std::string& p : job.paths) {
                std::string error;
                bool ok = true;
                const std::string name = fs::path(p).filename().string();
                std::string duplicate;
                if (job.op == Op::Copy || job.op == Op::Duplicate) {
                    const std::string to = freeName(job.dest, name);
                    ok = copyAny(p, to, error);
                    if (ok && job.op == Op::Duplicate) duplicate = to;
                }
                else if (job.op == Op::Move) {
                    if (fs::path(p).parent_path() == fs::path(job.dest)) ok = true;  // already there
                    else ok = moveAny(p, freeName(job.dest, name), error);
                }
                else if (job.op == Op::Trash) ok = trashOne(p, error);
                else if (job.op == Op::Restore) ok = restoreOne(p, error);
                else ok = deleteForever(p, error);
                std::lock_guard<std::mutex> lock(mutex);
                label = name;
                if (!duplicate.empty()) outputs.push_back(std::move(duplicate));
                done++;
                if (!ok) failure = name + ": " + error;
                changed = true;
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                running = false;
                busy = !jobs.empty();
                fraction = -1;
                changed = true;
            }
            wake();
        }
    }
};

// Detached so the launched application outlives this window.
void launchDetached(const char* program, const char* arg, const char* cwd = nullptr, bool exact = false) {
    const pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            setsid();
            if (cwd && chdir(cwd) != 0) _exit(126);
            const int null = open("/dev/null", O_RDWR);
            if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
            if (exact) execl(program, program, static_cast<char*>(nullptr));
            else if (arg) execlp(program, program, arg, static_cast<char*>(nullptr));
            else execlp(program, program, static_cast<char*>(nullptr));
        }
        _exit(0);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}
