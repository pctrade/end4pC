

std::string findCpuTempPath() {
    for (const char* want : {"coretemp", "k10temp", "zenpower", "cpu_thermal", "acpitz"}) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator("/sys/class/hwmon", ec)) {
            std::ifstream n(e.path() / "name");
            std::string name;
            std::getline(n, name);
            if (name == want && std::filesystem::exists(e.path() / "temp1_input")) return (e.path() / "temp1_input").string();
        }
    }
    return "";
}
bool updateResources(App& app) {
    App::Resources& R = app.res;
    const App::Resources before = R;
    const bool wide = app.resW.open() || app.game.surface || (app.clockP.open() && app.clockP.kind == 3);
    if (app.cfg.resCpu || wide) {
        std::ifstream f("/proc/stat");
        std::string cpu;
        unsigned long long u = 0, n = 0, sy = 0, idle = 0, iow = 0, irq = 0, sirq = 0, steal = 0;
        f >> cpu >> u >> n >> sy >> idle >> iow >> irq >> sirq >> steal;
        const unsigned long long tot = u + n + sy + idle + iow + irq + sirq + steal, idl = idle + iow;
        if (R.total && tot > R.total) R.cpu = 1.0 - double(idl - R.idle) / double(tot - R.total);
        R.idle = idl;
        R.total = tot;
    }
    if (app.cfg.resRam || app.cfg.resSwap || wide) {
        std::ifstream f("/proc/meminfo");
        std::string line;
        double memTotal = 0, memAvail = 0, swapTotal = 0, swapFree = 0;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string k;
            double v = 0;
            ss >> k >> v;
            if (k == "MemTotal:") memTotal = v;
            else if (k == "MemAvailable:") memAvail = v;
            else if (k == "SwapTotal:") swapTotal = v;
            else if (k == "SwapFree:") swapFree = v;
        }
        R.ram = memTotal > 0 ? 1.0 - memAvail / memTotal : 0;
        R.ramTotalGb = memTotal / 1048576.0;
        R.ramUsedGb = (memTotal - memAvail) / 1048576.0;
        R.swap = swapTotal > 0 ? 1.0 - swapFree / swapTotal : 0;
    }
    if (app.cfg.resDisk || wide) {
        struct statvfs sv {};
        if (statvfs("/", &sv) == 0 && sv.f_blocks > 0) {
            R.disk = double(sv.f_blocks - sv.f_bfree) / double(sv.f_blocks - sv.f_bfree + sv.f_bavail);
            R.diskTotalGb = sv.f_blocks * double(sv.f_frsize) / 1073741824.0;
            R.diskUsedGb = (sv.f_blocks - sv.f_bfree) * double(sv.f_frsize) / 1073741824.0;
        }
    }
    if (app.cfg.resTemp || wide) {
        if (R.tempPath.empty()) R.tempPath = findCpuTempPath();
        if (!R.tempPath.empty()) {
            std::ifstream f(R.tempPath);
            double milli = 0;
            f >> milli;
            R.temp = milli / 1000.0 / 100.0;
            R.tempC = milli / 1000.0;
        }
    }
    if (wide) {  // the graphics card: files of the kernel, or nvidia-smi (a program, so at most every two seconds)
        gpuinfo::Sample card = gpuinfo::readSysfs();
        if (!card.present && std::filesystem::exists("/proc/driver/nvidia/version")) {
            // nvidia-smi can take a while (or hang): it runs on its own thread, and the answer is picked up on a later call. `timeout -s KILL`: the
            // thread's signal mask is the one of the shell (SIGTERM blocked), which the program would inherit.
            static std::future<gpuinfo::Sample> job;
            if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) R.gpuNvidia = job.get();
            const auto now = std::chrono::steady_clock::now();
            if (!job.valid() && now - R.gpuAsked >= std::chrono::seconds(2)) {
                R.gpuAsked = now;
                job = std::async(std::launch::async, [] {
                    gpuinfo::Sample s;
                    if (FILE* p = popen("timeout -s KILL 2 nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r")) {
                        char line[160] = {};
                        if (std::fgets(line, sizeof line, p)) gpuinfo::parseNvidia(line, s);
                        pclose(p);
                    }
                    return s;
                });
            }
            card = R.gpuNvidia;
        }
        R.gpu = card;
        app.game.gpuRow = card.present;
    }
    auto moved = [](double a, double b) { return std::abs(a - b) > 0.005; };
    return moved(R.cpu, before.cpu) || moved(R.gpu.usage, before.gpu.usage) || moved(R.ram, before.ram) || moved(R.swap, before.swap) || moved(R.disk, before.disk) || moved(R.temp, before.temp);
}

void islandLiveBattery(App& app, int pct, int what);

bool updateBattery(App& app) {
    bool present = false, charging = false;
    int pct = 0;
    if (!app.batFake.empty()) {
        char st[32] = "";
        if (std::sscanf(app.batFake.c_str(), "%d %31s", &pct, st) >= 1) { present = true; charging = std::string(st) == "Charging" || std::string(st) == "Full"; }
    } else {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator("/sys/class/power_supply", ec)) {
            auto rd = [&](const char* f) { std::ifstream in(e.path() / f); std::string v; std::getline(in, v); return v; };
            if (rd("type") != "Battery" || rd("scope") == "Device") continue;
            const std::string cap = rd("capacity"), st = rd("status");
            if (cap.empty() || cap.find_first_not_of("0123456789") != std::string::npos) continue;
            present = true;
            pct = std::clamp(std::atoi(cap.c_str()), 0, 100);
            charging = st == "Charging" || st == "Full";
            break;
        }
    }
    {
        static int level = 0;
        if (!present || charging || pct > app.cfg.batteryLow) level = 0;
        else if (app.cfg.batteryNotify) {
            const int want = pct <= app.cfg.batteryCritical ? 2 : 1;
            if (want > level) {
                level = want;
                const std::string body = std::to_string(pct) + "% left" + (want == 2 ? ". Plug in the charger now." : ".");
                if (islandActive(app)) islandLiveBattery(app, pct, want == 2 ? 3 : 2);
                else runDetached({"notify-send", "-u", want == 2 ? "critical" : "normal", "-i", "battery-caution", "-a", "end4pC", want == 2 ? "Battery critical" : "Battery low", body.c_str()});
            }
        }
    }
    const bool changed = present != app.batPresent || pct != app.batPct || charging != app.batCharging;
    if (present && app.batPresent && islandActive(app)) {
        if (charging && !app.batCharging) islandLiveBattery(app, pct, 1);
        else if (charging && pct >= 100 && app.batPct < 100) islandLiveBattery(app, pct, 4);
    }
    app.batPresent = present; app.batPct = pct; app.batCharging = charging;
    return changed;
}

bool readPeripherals(App& app) {
    static const char* fake = std::getenv("END4PC_FAKE_PERIPHERALS");
    const char* x = std::getenv("XDG_CACHE_HOME");
    std::ifstream f((x && *x ? std::string(x) : homeDir() + "/.cache") + "/end4pC/peripherals.txt");
    std::vector<App::Periph> v;
    std::string line;
    std::istringstream fakeIn(fake ? std::string(fake) : std::string());
    std::istream& in = fake ? static_cast<std::istream&>(fakeIn) : f;
    while (std::getline(in, line, fake ? ';' : '\n')) {
        std::vector<std::string> p;
        std::stringstream ss(line);
        std::string t;
        while (std::getline(ss, t, '|')) p.push_back(t);
        if (p.size() < 4) continue;
        App::Periph d;
        d.name = p[0]; d.pct = std::atoi(p[1].c_str()); d.state = p[2]; d.kind = p[3];
        v.push_back(d);
    }
    const bool changed = v.size() != app.periph.size() || !std::equal(v.begin(), v.end(), app.periph.begin(), [](const App::Periph& a, const App::Periph& b) { return a.name == b.name && a.pct == b.pct && a.state == b.state; });
    app.periph = v;
    return changed;
}
const App::Periph* lowPeripheral(const App& app) {
    const App::Periph* worst = nullptr;
    for (const auto& d : app.periph)
        if (d.state != "charging" && d.state != "fully-charged" && d.pct > 0 && d.pct <= app.cfg.peripheralLow && (!worst || d.pct < worst->pct)) worst = &d;
    return worst;
}

// ---- UPower on the system bus: the batteries tell us when something changes (as every desktop does; Noctalia the same), no polling ----
// What the signals said since the loop last looked: `battery` (the laptop's own battery, the charger, UPower's OnBattery) and `devices` (the
// batteries of mice, keyboards, headsets ...: one changed, came or went).
struct UPowerWatch {
    sd_bus* bus = nullptr;
    bool battery = false, devices = false;
};
int onUPowerSignal(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* w = static_cast<UPowerWatch*>(userdata);
    const char* member = sd_bus_message_get_member(m);
    const char* path = sd_bus_message_get_path(m);
    if (member && (std::strcmp(member, "DeviceAdded") == 0 || std::strcmp(member, "DeviceRemoved") == 0)) { w->battery = w->devices = true; return 0; }
    const char* iface = nullptr;
    if (sd_bus_message_read_basic(m, 's', &iface) < 0) return 0;
    bool relevant = false;
    if (sd_bus_message_enter_container(m, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
            const char* name = nullptr;
            sd_bus_message_read_basic(m, 's', &name);
            if (name) for (const char* want : {"Percentage", "State", "IsPresent", "BatteryLevel", "WarningLevel", "OnBattery", "Online", "Type"})
                if (std::strcmp(name, want) == 0) relevant = true;
            sd_bus_message_skip(m, "v");
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }
    if (!relevant) return 0;
    const std::string p = path ? path : "";
    if (p == "/org/freedesktop/UPower" || p.find("DisplayDevice") != std::string::npos || p.find("/battery_BAT") != std::string::npos ||
        p.find("/line_power") != std::string::npos || p.find("/battery_CMB") != std::string::npos)
        w->battery = true;
    else
        w->devices = true;
    return 0;
}
bool startUPowerWatcher(UPowerWatch& w) {
    if (sd_bus_open_system(&w.bus) < 0) { w.bus = nullptr; return false; }
    const char* const matches[] = {
        "type='signal',sender='org.freedesktop.UPower',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',path_namespace='/org/freedesktop/UPower'",
        "type='signal',sender='org.freedesktop.UPower',interface='org.freedesktop.UPower',member='DeviceAdded'",
        "type='signal',sender='org.freedesktop.UPower',interface='org.freedesktop.UPower',member='DeviceRemoved'",
    };
    for (const char* match : matches)
        if (sd_bus_add_match(w.bus, nullptr, match, onUPowerSignal, &w) < 0) { sd_bus_unref(w.bus); w.bus = nullptr; return false; }
    return true;
}

std::string volLimit(const App& app) { char b[16]; std::snprintf(b, sizeof b, "%.2f", app.cfg.volumeMax / 100.0); return b; }
std::string volStep(const App& app, bool down) { return std::to_string(app.cfg.volumeStep) + (down ? "%-" : "%+"); }
// The user name of the account, and the name the person is shown with (src/identity.h): the display name, or the user name when there is none.
std::string systemUserName() { const struct passwd* pw = getpwuid(getuid()); return pw && pw->pw_name ? pw->pw_name : "user"; }
std::string shownName(const App& app) { return identity::displayName(app.cfg.displayName, systemUserName()); }
// The volume of the sound card can go above 100 % (a keybind with `wpctl -l 1.5`, pavucontrol, the Volume limit raised above 100):
// the shell shows it as it is. A slider spans 0..volScale: 100, or the limit when that is higher, with a mark where 100 % is.
int volScale(const App& app) { return std::max(100, app.cfg.volumeMax); }
double volFrac(const App& app, int pct) { return std::clamp(pct / static_cast<double>(volScale(app)), 0.0, 1.0); }
int volFromFrac(const App& app, double f) { return std::clamp(static_cast<int>(std::lround(std::clamp(f, 0.0, 1.0) * volScale(app))), 0, volScale(app)); }
bool volLoud(int pct) { return pct > 100; }  // above 100 % the sound can distort: the fill changes colour
double volMark(const App& app) { return volScale(app) > 100 ? 100.0 / volScale(app) : -1; }  // where 100 % sits on a slider, or -1 when nothing is above it

double clockBarPaint(cairo_t* cr, const App& app, PangoLayout* layout, double bx, double barH, bool hover);

bool updateClock(App& app) {
    timespec nowTs{};  // (time() reads the coarse clock, which can still say 12:59:59 when the minute timer fires: the bar then stayed a minute behind)
    clock_gettime(CLOCK_REALTIME, &nowTs);
    std::time_t now = nowTs.tv_sec;
    std::tm tm{};
    localtime_r(&now, &tm);
    char t[16], d[32];
    std::strftime(t, sizeof t, app.cfg.clock24 ? "%H:%M" : "%-I:%M", &tm);
    i18n::strftime(d, sizeof d, app.cfg.clockDate ? "%a %d %b" : "", &tm);
    char ap[8] = "";
    if (!app.cfg.clock24) std::strftime(ap, sizeof ap, "%p", &tm);
    if (app.clockTime == t && app.clockDate == d && app.clockAmPm == ap) return false;
    app.clockTime = t;
    app.clockDate = d;
    app.clockAmPm = ap;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    app.clockW = static_cast<int>(std::ceil(clockBarPaint(nullptr, app, layout, 0, kBarHeight, false)));
    {
        PangoFontDescription* fd = pango_font_description_from_string(kClockTimeFont);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_text(layout, app.clockTime.c_str(), -1);
        int iw = 0, ih = 0;
        pango_layout_get_pixel_size(layout, &iw, &ih);
        app.islandTimeW = iw;
        pango_font_description_free(fd);
    }
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return true;
}

void measureMusic(App& app) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    int widest = 0, titleW = 0;
    const std::string artistText = musicArtistShown(app), titleText = musicTitleShown(app);
    const std::pair<const char*, const std::string*> lines[2] = {{kArtistFont, &artistText}, {kTitleFont, &titleText}};
    for (const auto& [fontName, text] : lines) {
        PangoFontDescription* fd = pango_font_description_from_string(fontName);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_text(layout, text->c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout, &w, &h);
        widest = std::max(widest, w);
        if (text == &titleText) titleW = w;
        pango_font_description_free(fd);
    }
    int m3Widest = 0;
    for (const auto& [fontName, text] : {std::pair<const char*, const std::string*>{"Sans 9", &artistText}, {"Sans 9.75", &titleText}}) {
        PangoFontDescription* fd = pango_font_description_from_string(fontName);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_text(layout, text->c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout, &w, &h);
        m3Widest = std::max(m3Widest, w);
        pango_font_description_free(fd);
    }
    app.musicM3TextW = std::min(m3Widest + 2, static_cast<int>(kMediaTextMax));
    {
        PangoFontDescription* fd = pango_font_description_from_string(kProgFont);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_text(layout, musicLineShown(app).c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout, &w, &h);
        app.musicLineW = std::clamp(w + 2, kMusicTextMin, kProgLineMax);
        pango_font_description_free(fd);
    }
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    app.musicTextW = std::clamp(widest + 2, kMusicTextMin, kMusicTextMax);  // +2: antialiasing slack
    app.musicTitleW = std::clamp(titleW + 2, kMusicTextMin, kMusicTextMax);
}

void islandLiveTrack(App& app);
bool parseMusicLine(App& app, const std::string& line) {
    std::string f[5];
    size_t start = 0;
    for (int i = 0; i < 5; i++) {
        const size_t tab = i < 4 ? line.find('\t', start) : std::string::npos;
        f[i] = line.substr(start, tab == std::string::npos ? std::string::npos : tab - start);
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    // MPRIS bus-name suffixes cannot contain shell metacharacters. Keep the exact
    // instance, rather than selecting %any again for each command or progress poll.
    std::string player = f[0];
    std::string status = f[1];
    if (player.empty() || player.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") != std::string::npos ||
        (status != "Playing" && status != "Paused" && status != "Stopped")) {
        player.clear();
        status.clear();
        f[2].clear(); f[3].clear(); f[4].clear();
    }
    const bool playerChanged = player != app.musicPlayer;
    const bool trackChanged = !status.empty() && !app.musicTitle.empty() && (f[4] != app.musicTitle || f[3] != app.musicArtist);
    if (playerChanged || trackChanged || !f[2].empty()) app.musicArtFallback.clear();
    const std::string cover = f[2].empty() ? app.musicArtFallback : f[2];
    const bool coverBefore = app.musicArt != nullptr;
    const bool artChanged = app.musicArtUrl != cover;
    updateCover(app, cover);
    const bool changed = playerChanged || artChanged || status != app.musicStatus || f[3] != app.musicArtist || f[4] != app.musicTitle ||
                         coverBefore != (app.musicArt != nullptr);
    app.musicPlayer = player;
    app.musicStatus = status;
    app.musicArtist = f[3];
    app.musicTitle = f[4];
    if (playerChanged || trackChanged) {
        app.musicPlaybackPlayer = player;
        app.media.position = app.media.length = 0;
        app.media.status.clear();
        app.media.drag = 0;
        app.media.nextPoll = std::chrono::steady_clock::now();
    }
    if (changed) measureMusic(app);
    if (trackChanged) islandLiveTrack(app);
    return changed;
}

std::string netRateText(double rate, bool compact) {
    static const char* full[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    static const char* shortU[] = {"B", "K", "M", "G"};
    double v = std::max(0.0, rate);
    int u = 0;
    while (v >= 1024 && u < 3) { v /= 1024; u++; }
    char b[32];
    std::snprintf(b, sizeof b, compact ? "%.*f%s" : "%.*f %s", u > 0 && v < 99.95 ? 1 : 0, v, compact ? shortU[u] : full[u]);
    return b;
}
void readNetBytes(unsigned long long& rx, unsigned long long& tx) {
    std::ifstream f("/proc/net/dev");
    std::string line;
    rx = 0;
    tx = 0;
    while (std::getline(f, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(' '));
        if (name == "lo") continue;
        std::istringstream ss(line.substr(colon + 1));
        unsigned long long v[9] = {};
        for (auto& x : v) ss >> x;
        rx += v[0];
        tx += v[8];
    }
}
bool updateNetSpeed(App& app) {
    unsigned long long rx = 0, tx = 0;
    readNetBytes(rx, tx);
    const auto now = std::chrono::steady_clock::now();
    if (app.netPrevAt != std::chrono::steady_clock::time_point{}) {
        const double dt = std::chrono::duration<double>(now - app.netPrevAt).count();
        if (dt > 0.05) {
            app.netDown = rx >= app.netPrevRx ? (rx - app.netPrevRx) / dt : 0;
            app.netUp = tx >= app.netPrevTx ? (tx - app.netPrevTx) / dt : 0;
        }
    }
    app.netPrevRx = rx;
    app.netPrevTx = tx;
    app.netPrevAt = now;
    const std::string text = netRateText(app.netUp, false) + "|" + netRateText(app.netDown, false) + (app.netUp > 0 ? "u" : "-") + (app.netDown > 0 ? "d" : "-");
    const bool changed = text != app.netText;
    app.netText = text;
    return changed;
}
