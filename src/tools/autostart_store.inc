// Tools: the apps that start with the session (page_autostart.inc). They live in ~/.local/share/end4pC/autostart.tsv, one app per line:
//   enabled (1/0) TAB workspace (0 = wherever the window opens, -1 = the special workspace) TAB name TAB icon TAB command
// scripts/autostart.py starts them, in this order, when the session starts (the shell runs it once per login).
// Included by src/tools.cpp (inside its anonymous namespace).

constexpr int kAsMaxWs = 20;      // two monitors: ten workspaces each
constexpr int kAsSpecial = -1;    // the special workspace of Hyprland (the app opens hidden there)

struct AsApp {
    bool on = true;
    int ws = 0;
    std::string name, icon, cmd;
};
struct AsDesk {
    std::string id, name, icon, cmd, path, wmClass;  // path: the .desktop file (cmd is only what is shown); wmClass: StartupWMClass
};

std::string asPath() { return configDir() + "/autostart.tsv"; }

std::string asClean(std::string s) {  // (a tab or a line break would break the table)
    for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

std::vector<AsApp> asLoad() {
    std::vector<AsApp> out;
    std::ifstream in(asPath());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t p = 0;
        for (;;) {
            const size_t t = line.find('\t', p);
            if (t == std::string::npos) { f.push_back(line.substr(p)); break; }
            f.push_back(line.substr(p, t - p));
            p = t + 1;
        }
        if (f.size() < 5 || f[4].empty()) continue;
        AsApp a;
        a.on = f[0] != "0";
        a.ws = std::clamp(std::atoi(f[1].c_str()), kAsSpecial, kAsMaxWs);
        a.name = f[2];
        a.icon = f[3];
        a.cmd = f[4];
        out.push_back(a);
    }
    return out;
}

bool asSave(const std::vector<AsApp>& rows) {
    ::mkdir(configDir().c_str(), 0755);
    const std::string tmp = asPath() + ".tmp";
    {
        std::ofstream o(tmp, std::ios::trunc);
        if (!o) return false;
        o << "# end4pC autostart: enabled, workspace (0 = any), name, icon, command (Tools > Autostart edits this file)\n";
        for (const AsApp& a : rows) o << (a.on ? "1" : "0") << '\t' << a.ws << '\t' << asClean(a.name) << '\t' << asClean(a.icon) << '\t' << asClean(a.cmd) << '\n';
        if (!o) return false;
    }
    return std::rename(tmp.c_str(), asPath().c_str()) == 0;
}

// An Exec= line without its field codes (%U, %f...): what can be run as it is.
std::string asExec(const std::string& exec) {
    std::string out;
    for (size_t i = 0; i < exec.size(); i++) {
        if (exec[i] != '%' || i + 1 >= exec.size()) { out += exec[i]; continue; }
        const char c = exec[++i];
        if (c == '%') out += '%';  // (any other code is dropped)
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// The command that starts an app of a desktop entry: `gio launch` reads the file as the desktop does (its Exec quoting and escapes, Path, the terminal).
std::string asLaunch(const std::string& desktopFile) {
    std::string q = "'";
    for (const char c : desktopFile) { if (c == '\'') q += "'\\''"; else q += c; }
    return "gio launch " + q + "'";
}

// The installed apps (desktop entries that are shown in menus), by name. The entries of Wine programs and the like live in folders under applications/.
void asScan(const std::string& dir, const std::string& idPrefix, int depth, std::map<std::string, AsDesk>& byId) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> subs;
    while (const dirent* e = readdir(d)) {
        const std::string f = e->d_name;
        if (f.empty() || f[0] == '.') continue;
        struct stat st{};
        if (stat((dir + "/" + f).c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 5) subs.push_back(f); continue; }
        if (f.size() < 9 || f.compare(f.size() - 8, 8, ".desktop") != 0) continue;
        std::ifstream in(dir + "/" + f);
        std::string line, exec;
        AsDesk a;
        bool main = false, skip = false;
        while (std::getline(in, line)) {
            if (!line.empty() && line[0] == '[') { main = line == "[Desktop Entry]"; continue; }
            if (!main) continue;
            if (line.rfind("Name=", 0) == 0 && a.name.empty()) a.name = line.substr(5);
            else if (line.rfind("Icon=", 0) == 0) a.icon = line.substr(5);
            else if (line.rfind("StartupWMClass=", 0) == 0) a.wmClass = line.substr(15);
            else if (line.rfind("Exec=", 0) == 0 && exec.empty()) exec = line.substr(5);
            else if (line == "NoDisplay=true" || line == "Hidden=true") skip = true;
            else if (line.rfind("Type=", 0) == 0 && line != "Type=Application") skip = true;
        }
        a.cmd = asExec(exec);
        if (skip || a.name.empty() || a.cmd.empty()) continue;
        a.id = idPrefix + f.substr(0, f.size() - 8);
        a.path = dir + "/" + f;
        byId[a.id] = a;  // (a later folder, the user's own, wins)
    }
    closedir(d);
    for (const std::string& sub : subs) asScan(dir + "/" + sub, idPrefix + sub + "-", depth + 1, byId);
}
std::vector<AsDesk> asInstalled() {
    std::map<std::string, AsDesk> byId;
    const std::vector<std::string> dirs = {"/usr/share/applications", "/usr/local/share/applications", "/var/lib/flatpak/exports/share/applications",
                                           homeDir() + "/.local/share/flatpak/exports/share/applications", envDir("XDG_DATA_HOME", homeDir() + "/.local/share") + "/applications"};
    for (const std::string& d : dirs) asScan(d, "", 0, byId);
    std::vector<AsDesk> out;
    for (auto& [id, a] : byId) out.push_back(std::move(a));
    std::sort(out.begin(), out.end(), [](const AsDesk& x, const AsDesk& y) {
        std::string a = x.name, b = y.name;
        for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return a < b;
    });
    return out;
}

// ---- the apps that are open now (Hyprland): "Save open apps" -----------------------------------------------------------------------------

std::string asLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string asQuote(const std::string& s) {  // one word for sh
    if (!s.empty() && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-./:=@%+,") == std::string::npos) return s;
    std::string q = "'";
    for (const char c : s) { if (c == '\'') q += "'\\''"; else q += c; }
    return q + "'";
}

struct AsOpen {
    AsApp app;
    bool known = false;  // true: found as a desktop entry; false: only the command line of the process
    bool dup = false;    // already in the list
};

// The desktop entry of a window: by its StartupWMClass, its file name (also the last part of it) or the program of its Exec.
const AsDesk* asMatch(const std::vector<AsDesk>& all, const std::string& cls, const std::string& initial) {
    for (const std::string& c : {initial, cls}) {
        const std::string l = asLower(c);
        if (l.empty()) continue;
        for (const AsDesk& e : all) if (!e.wmClass.empty() && asLower(e.wmClass) == l) return &e;
        for (const AsDesk& e : all) if (asLower(e.id) == l) return &e;
        for (const AsDesk& e : all) { const std::string id = asLower(e.id); if (id.size() > l.size() && id.compare(id.size() - l.size() - 1, std::string::npos, "." + l) == 0) return &e; }
        for (const AsDesk& e : all) {
            const size_t sp = e.cmd.find(' ');
            std::string prog = e.cmd.substr(0, sp);
            const size_t slash = prog.rfind('/');
            if (slash != std::string::npos) prog = prog.substr(slash + 1);
            if (asLower(prog) == l) return &e;
        }
    }
    return nullptr;
}

std::string asProcCmdline(int pid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string out, arg;
    for (const char c : raw) {
        if (c != '\0') { arg += c; continue; }
        if (!out.empty()) out += ' ';
        out += asQuote(arg);
        arg.clear();
    }
    return out;
}

// What is open on the screen now, one entry per app and workspace. Without Hyprland (or jq) there is nothing to read.
std::vector<AsOpen> asCaptureOpen(const std::vector<AsApp>& existing) {
    std::vector<AsOpen> out;
    if (!std::getenv("HYPRLAND_INSTANCE_SIGNATURE")) return out;
    std::string raw;
    if (!trRunSync({"sh", "-c", "hyprctl -j clients 2>/dev/null | jq -r '.[] | select(.mapped) | [.class, .initialClass, (.pid | tostring), (.workspace.id | tostring), .workspace.name] | @tsv'"}, "", &raw, 4000)) return out;
    const std::vector<AsDesk> installed = asInstalled();
    std::istringstream in(raw);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        size_t p = 0;
        for (;;) {
            const size_t t = line.find('\t', p);
            if (t == std::string::npos) { f.push_back(line.substr(p)); break; }
            f.push_back(line.substr(p, t - p));
            p = t + 1;
        }
        if (f.size() < 5 || (f[0].empty() && f[1].empty())) continue;
        // The shell's own windows (Settings, Tools, the lock screen...) are not apps to start; Play3r, Fil3s and T3xt are programs of their own and are.
        auto ownWindow = [](const std::string& c) { return c.rfind("end4pC", 0) == 0 && c != "end4pC-play3r" && c != "end4pC-fil3s" && c != "end4pC-t3xt"; };
        if (ownWindow(f[0]) || ownWindow(f[1])) continue;
        AsOpen o;
        const int wsId = std::atoi(f[3].c_str());
        o.app.ws = f[4].rfind("special", 0) == 0 ? kAsSpecial : wsId > 0 ? std::min(wsId, kAsMaxWs) : 0;
        if (const AsDesk* e = asMatch(installed, f[0], f[1])) {
            o.known = true;
            o.app.name = e->name;
            o.app.icon = e->icon;
            o.app.cmd = asLaunch(e->path);
        } else {
            o.app.cmd = asProcCmdline(std::atoi(f[2].c_str()));
            o.app.name = f[0].empty() ? f[1] : f[0];
            o.app.icon = asLower(o.app.name);
        }
        if (o.app.cmd.empty()) continue;
        const bool same = std::any_of(out.begin(), out.end(), [&](const AsOpen& x) { return x.app.cmd == o.app.cmd && x.app.ws == o.app.ws; });
        if (same) continue;
        o.dup = std::any_of(existing.begin(), existing.end(), [&](const AsApp& x) { return x.cmd == o.app.cmd && x.ws == o.app.ws; });
        out.push_back(o);
    }
    const auto key = [](int ws) { return ws > 0 ? ws : ws == kAsSpecial ? 99 : 0; };
    std::stable_sort(out.begin(), out.end(), [&](const AsOpen& a, const AsOpen& b) { return key(a.app.ws) < key(b.app.ws); });
    return out;
}
