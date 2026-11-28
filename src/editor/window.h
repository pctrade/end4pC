// What T3xt does and its Wayland window: the documents, the keys, the mouse, find and replace, files, the clipboard, the loop.
// Included once by editor.cpp, inside its anonymous namespace (after ui.h).
#pragma once

void say(const std::string& msg) { g.toast = msg; g.toastAt = Clock::now(); g.dirty = true; }

#include "../gui/proc.h"

void copyToClipboard(const std::string& text) {
    if (text.empty()) return;
    g.clipboard = text;
    runWrite({"wl-copy"}, text);
}
std::string pasteFromClipboard() {
    bool ok = false;
    const std::string s = runCapture({"wl-paste", "--no-newline"}, 800, ok);
    return ok ? s : g.clipboard;
}

// ---- the settings kept between runs ----------------------------------------------------------------------------------------------------
std::string settingsPath() { return dataDir() + "/t3xt.txt"; }
void loadSettings() {
    std::ifstream f(settingsPath());
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("sidebar=", 0) == 0) g.sidebar = line.substr(8) != "0";
        else if (line.rfind("restore=", 0) == 0) g.restoreSession = line.substr(8) != "0";
        else if (line.rfind("font=", 0) == 0) g.fontPx = std::clamp(std::atoi(line.c_str() + 5), 9, 40);
    }
}
void saveSettings() {
    std::error_code ec;
    fs::create_directories(dataDir(), ec);
    std::ofstream f(settingsPath(), std::ios::trunc);
    f << "sidebar=" << (g.sidebar ? 1 : 0) << "\nfont=" << g.fontPx << "\nrestore=" << (g.restoreSession ? 1 : 0) << "\n";
}

// ---- the session: the files that were open are open again next time (like Geany) --------------------------------------------------------
std::string sessionPath() { return dataDir() + "/t3xt-session.txt"; }
void saveSession() {
    std::error_code ec;
    fs::create_directories(dataDir(), ec);
    const std::string tmp = sessionPath() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        int saved = 0, activeSaved = -1;
        std::ostringstream body;
        for (size_t i = 0; i < g.docs.size(); i++) {
            const t3::Doc& d = *g.docs[i];
            if (d.path.empty()) continue;  // (an unnamed document is not a file: it is not kept)
            if (static_cast<int>(i) == g.active) activeSaved = saved;
            body << d.path << '\t' << d.cur.line << '\t' << d.cur.col << '\t' << static_cast<long>(d.scrollY) << '\n';
            saved++;
        }
        f << "active=" << std::max(0, activeSaved) << '\n' << body.str();
    }
    fs::rename(tmp, sessionPath(), ec);
}

void openPath(const std::string& given);
void switchTo(int i);
// Opens again what the last session had: each file at the line where the cursor was, and the tab that was in front.
void restoreSession() {
    std::ifstream f(sessionPath());
    std::string line;
    if (!std::getline(f, line) || line.rfind("active=", 0) != 0) return;
    const int wanted = std::max(0, std::atoi(line.c_str() + 7));
    std::vector<size_t> opened;  // the documents this restored, in order
    while (std::getline(f, line) && opened.size() < 40) {
        std::stringstream ss(line);
        std::string path, a, b, c;
        std::getline(ss, path, '\t'); std::getline(ss, a, '\t'); std::getline(ss, b, '\t'); std::getline(ss, c, '\t');
        std::error_code ec;
        if (path.empty() || path[0] != '/' || !fs::is_regular_file(path, ec)) continue;
        const size_t before = g.docs.size();
        openPath(path);
        if (g.docs.empty() || g.docs[static_cast<size_t>(g.active)]->path.empty()) continue;
        t3::Doc& d = doc();
        d.cur = d.anchor = d.clampPos({std::atoi(a.c_str()), std::atoi(b.c_str())});
        d.scrollY = static_cast<double>(std::max(0, std::atoi(c.c_str())));
        opened.push_back(static_cast<size_t>(g.active));
        (void)before;
    }
    if (!opened.empty()) switchTo(static_cast<int>(opened[std::min(static_cast<size_t>(wanted), opened.size() - 1)]));
}

// One window only, like Geany: a second launch hands its files to the first one (over a socket in the runtime folder) and exits.
std::string socketPath() { return runtimeDir() + "/end4pC-t3xt.sock"; }
int g_listenFd = -1;
bool handOffToRunning(int argc, char** argv) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::string path = socketPath();
    if (path.size() >= sizeof addr.sun_path) { close(fd); return false; }
    std::strcpy(addr.sun_path, path.c_str());
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) { close(fd); return false; }
    std::string msg;
    for (int i = 1; i < argc; i++) {
        char real[4096];
        const std::string abs = realpath(argv[i], real) ? std::string(real) : (argv[i][0] == '/' ? std::string(argv[i]) : std::string());
        if (!abs.empty() && abs.find('\n') == std::string::npos) msg += abs + "\n";
    }
    if (argc <= 1) msg = "NEW\n";
    for (size_t done = 0; done < msg.size();) {
        const ssize_t n = write(fd, msg.data() + done, msg.size() - done);
        if (n <= 0) break;
        done += static_cast<size_t>(n);
    }
    close(fd);
    return true;
}
void listenForHandOffs() {
    const std::string path = socketPath();
    unlink(path.c_str());  // (a socket left by a run that crashed)
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) { close(fd); return; }
    std::strcpy(addr.sun_path, path.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(fd, 8) != 0) { close(fd); return; }
    chmod(path.c_str(), 0600);
    g_listenFd = fd;
}
void addEmptyDoc();
// What another launch sent: files to open (each line a path), or NEW for an empty document.
void acceptHandOffs() {
    if (g_listenFd < 0) return;
    while (true) {
        const int c = accept4(g_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0) break;
        std::string data;
        char buf[4096];
        pollfd p{c, POLLIN, 0};
        while (data.size() < 64 * 1024 && poll(&p, 1, 200) > 0) {  // (a client that says nothing does not hold the window)
            const ssize_t n = read(c, buf, sizeof buf);
            if (n <= 0) break;
            data.append(buf, static_cast<size_t>(n));
        }
        close(c);
        std::stringstream ss(data);
        std::string line;
        while (std::getline(ss, line)) {
            if (line == "NEW") { const bool blank = g.docs.size() && doc().path.empty() && !doc().modified; if (!blank) addEmptyDoc(); }
            else if (!line.empty() && line[0] == '/') openPath(line);
        }
        g.dirty = true;
    }
}

// ---- the sidebar's folder --------------------------------------------------------------------------------------------------------------
void readSideDir() {
    g.sideEntries.clear();
    std::error_code ec;
    for (fs::directory_iterator it(g.sideDir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end && g.sideEntries.size() < 3000; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code e2;
        const bool dir = it->is_directory(e2);
        if (!dir && !it->is_regular_file(e2)) continue;
        g.sideEntries.push_back({name, it->path().string(), dir});
    }
    std::sort(g.sideEntries.begin(), g.sideEntries.end(), [](const SideEntry& a, const SideEntry& b) {
        if (a.dir != b.dir) return a.dir;
        return t3::lowerStr(a.name) < t3::lowerStr(b.name);
    });
    g.sidePos = 0;
    g.sideBand.reset();
    g.dirty = true;
}
void followActiveFile() {
    const std::string& p = doc().path;
    if (p.empty()) return;
    const std::string dir = fs::path(p).parent_path().string();
    if (!dir.empty() && dir != g.sideDir) { g.sideDir = dir; readSideDir(); }
}

// ---- the documents ---------------------------------------------------------------------------------------------------------------------
void ensureCursorVisible() {
    t3::Doc& d = doc();
    const double top = d.cur.line * g.lineH, viewH = g.textRect.h > 0 ? g.textRect.h : 400;
    if (top < d.scrollY) d.scrollY = top;
    else if (top + g.lineH > d.scrollY + viewH) d.scrollY = top + g.lineH - viewH;
    const double cx = colX(d, d.cur.line, d.cur.col), viewW = g.textRect.w > 0 ? g.textRect.w : 600;
    if (cx < d.scrollX) d.scrollX = std::max(0.0, cx - 60);
    else if (cx > d.scrollX + viewW - 24) d.scrollX = cx - viewW + 80;
    g.caretReset = Clock::now();
    g.caretOn = true;
    g.dirty = true;
}
void switchTo(int i) {
    if (i < 0 || i >= static_cast<int>(g.docs.size())) return;
    g.active = i;
    g.band.reset();
    followActiveFile();
    saveSession();
    g.dirty = true;
}
void addEmptyDoc() {
    g.docs.push_back(std::make_unique<t3::Doc>());
    switchTo(static_cast<int>(g.docs.size()) - 1);
}
// Opens a file in a tab (or goes to the tab it is already in). A name that does not exist becomes a new file with that name.
void openPath(const std::string& given) {
    std::error_code ec;
    std::string path = fs::weakly_canonical(given, ec).string();
    if (ec) path = given;
    for (size_t i = 0; i < g.docs.size(); i++) if (g.docs[i]->path == path) { switchTo(static_cast<int>(i)); return; }
    auto d = std::make_unique<t3::Doc>();
    std::string error;
    if (!fs::exists(path, ec)) { d->path = path; d->detect(); }
    else if (!d->load(path, error)) { say(fs::path(path).filename().string() + ": " + error); return; }
    else if (d->readOnly) say(std::string(i18n::tr("Opened read-only: the file is not valid UTF-8")));
    // an untouched empty document that is the only one is replaced by the file
    if (g.docs.size() == 1 && g.docs[0]->path.empty() && !g.docs[0]->modified && g.docs[0]->lines.size() == 1 && g.docs[0]->lines[0].empty()) g.docs.clear();
    g.docs.push_back(std::move(d));
    switchTo(static_cast<int>(g.docs.size()) - 1);
    g.sideDir.clear();
    followActiveFile();
}
void closeDoc(int i) {
    if (i < 0 || i >= static_cast<int>(g.docs.size())) return;
    g.docs.erase(g.docs.begin() + i);
    if (g.docs.empty()) addEmptyDoc();
    g.active = std::clamp(g.active > i ? g.active - 1 : g.active, 0, static_cast<int>(g.docs.size()) - 1);
    followActiveFile();
    saveSession();
    g.dirty = true;
}

// What to do once a save-as dialog has answered (the dialog runs on its own, so the window never freezes).
enum class After { Nothing, CloseDoc, Quit };
After g_after = After::Nothing;
int g_afterDoc = -1;
void closeWindowChecked();

void startDialogThread(std::vector<std::string> argv, bool saving) {
    std::thread([argv = std::move(argv), saving] {
        bool ok = false;
        std::string out = runCapture(argv, 0, ok);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        {
            std::lock_guard<std::mutex> lock(g.chosenMutex);
            if (ok && !out.empty()) {
                if (saving) g.chosenSave = out;
                else { std::stringstream ss(out); std::string line; while (std::getline(ss, line)) if (!line.empty()) g.chosenOpen.push_back(line); }
            } else if (saving) g.chosenSave = "\x01";  // cancelled: the pending action is dropped
        }
        const char c = 1;
        if (write(g_wakeFd, &c, 1) < 0) {}
    }).detach();
}
void chooseFiles() {
    startDialogThread({"zenity", "--file-selection", "--multiple", "--separator=\n", "--title=T3xt"}, false);
}
void saveDocAs(int i, After after = After::Nothing) {
    if (i < 0 || i >= static_cast<int>(g.docs.size())) return;
    g_after = after;
    g_afterDoc = i;
    const t3::Doc& d = *g.docs[static_cast<size_t>(i)];
    const std::string start = (d.path.empty() ? (g.sideDir.empty() ? homeDir() : g.sideDir) + "/" + d.title() : d.path);
    startDialogThread({"zenity", "--file-selection", "--save", "--confirm-overwrite", "--filename=" + start, "--title=T3xt"}, true);
}
// Saves a document; an unnamed one asks for a name first. True when it was written right now.
bool saveDoc(int i, After after = After::Nothing) {
    if (i < 0 || i >= static_cast<int>(g.docs.size())) return false;
    t3::Doc& d = *g.docs[static_cast<size_t>(i)];
    if (d.path.empty()) { saveDocAs(i, after); return false; }
    std::string error;
    if (!d.save("", error)) { say(d.title() + ": " + error); return false; }
    if (i == g.active) followActiveFile();
    g.dirty = true;
    return true;
}
void afterSaved() {
    const After a = g_after;
    const int i = g_afterDoc;
    g_after = After::Nothing;
    g_afterDoc = -1;
    if (a == After::CloseDoc) closeDoc(i);
    else if (a == After::Quit) closeWindowChecked();
}

// Closing: a document with unsaved changes asks first.
void requestCloseDoc(int i) {
    if (i < 0 || i >= static_cast<int>(g.docs.size())) return;
    if (!g.docs[static_cast<size_t>(i)]->modified) { closeDoc(i); return; }
    g.dialog = Dlg::CloseTab;
    g.dialogDoc = i;
    g.dirty = true;
}
void closeWindowChecked() {
    for (size_t i = 0; i < g.docs.size(); i++)
        if (g.docs[i]->modified) { g.dialog = Dlg::Quit; g.dialogDoc = static_cast<int>(i); g.dirty = true; return; }
    g.running = false;
}
void dialogAnswer(int id) {
    const Dlg kind = g.dialog;
    const int i = g.dialogDoc;
    g.dialog = Dlg::None;
    g.dirty = true;
    if (id == DlgCancel || i < 0 || i >= static_cast<int>(g.docs.size())) return;
    if (id == DlgDiscard) {
        if (kind == Dlg::CloseTab) closeDoc(i);
        else { g.docs[static_cast<size_t>(i)]->modified = false; closeWindowChecked(); }
        return;
    }
    // Save
    if (saveDoc(i, kind == Dlg::CloseTab ? After::CloseDoc : After::Quit)) {
        if (kind == Dlg::CloseTab) closeDoc(i); else closeWindowChecked();
    }
}

// ---- find and replace ------------------------------------------------------------------------------------------------------------------
void openFind(bool withReplace) {
    g.findOpen = true;
    g.replaceOpen = withReplace || g.replaceOpen;
    g.focus = 1;
    const std::string sel = doc().selectedText();
    if (!sel.empty() && sel.find('\n') == std::string::npos && sel.size() < 200) g.find = {sel, sel.size()};
    g.find.cur = g.find.s.size();
    g.caretReset = Clock::now();
    g.dirty = true;
}
void closeFind() { g.findOpen = false; g.replaceOpen = false; g.focus = 0; g.dirty = true; }
void findNext(bool forward, bool fromStart = false) {
    t3::Doc& d = doc();
    if (g.find.s.empty()) return;
    t3::Pos from = fromStart ? d.selStart() : (forward ? d.selEnd() : d.selStart());
    t3::Pos found;
    if (!d.find(g.find.s, g.matchCase, from, forward, found)) { say(i18n::tr("No matches")); return; }
    d.anchor = found;
    d.cur = {found.line, found.col + static_cast<int>(g.find.s.size())};
    d.wantVis = -1;
    ensureCursorVisible();
}
void replaceOne() {
    t3::Doc& d = doc();
    if (d.readOnly || g.find.s.empty()) return;
    const std::string sel = d.selectedText();
    const bool matches = g.matchCase ? sel == g.find.s : t3::lowerStr(sel) == t3::lowerStr(g.find.s);
    if (matches) {
        const t3::Pos a = d.selStart();
        const t3::Pos end = d.replaceRange(a, d.selEnd(), g.replace.s, true, 0);
        d.cur = d.anchor = end;
        d.fixCursorAfter();
    }
    findNext(true);
}
void replaceEverything() {
    t3::Doc& d = doc();
    if (d.readOnly || g.find.s.empty()) return;
    const int n = d.replaceAll(g.find.s, g.replace.s, g.matchCase);
    say(std::string(i18n::tr("Replaced")) + ": " + std::to_string(n));
    ensureCursorVisible();
}

// ---- editing keys ----------------------------------------------------------------------------------------------------------------------
size_t fieldPrev(const std::string& s, size_t i) { return static_cast<size_t>(t3::prevCol(s, static_cast<int>(i))); }
size_t fieldNext(const std::string& s, size_t i) { return static_cast<size_t>(t3::nextCol(s, static_cast<int>(i))); }

bool modActive(const char* name) { return g.xkbState && xkb_state_mod_name_is_active(g.xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0; }

// The text a key makes: with the dead keys and the compose key of the layout (so ´ then a is á). `consumed` is set when the key only
// started or continued a sequence.
std::string keyText(uint32_t key, xkb_keysym_t sym, bool& consumed) {
    consumed = false;
    if (g.composeState && !modActive(XKB_MOD_NAME_CTRL) && !modActive(XKB_MOD_NAME_ALT)) {
        xkb_compose_state_feed(g.composeState, sym);
        switch (xkb_compose_state_get_status(g.composeState)) {
            case XKB_COMPOSE_COMPOSING: consumed = true; return "";
            case XKB_COMPOSE_CANCELLED: xkb_compose_state_reset(g.composeState); consumed = true; return "";
            case XKB_COMPOSE_COMPOSED: {
                char buf[64];
                const int n = xkb_compose_state_get_utf8(g.composeState, buf, sizeof buf);
                xkb_compose_state_reset(g.composeState);
                return n > 0 ? std::string(buf, static_cast<size_t>(n)) : "";
            }
            default: break;
        }
    }
    char buf[16];
    const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f) return "";
    return std::string(buf, static_cast<size_t>(n));
}

// A key while a field of the find bar has the keyboard. True when it can repeat.
bool fieldKey(uint32_t key, xkb_keysym_t sym, bool ctrl, bool shift) {
    Field& f = g.focus == 1 ? g.find : g.replace;
    f.cur = std::min(f.cur, f.s.size());
    g.caretReset = Clock::now();
    g.dirty = true;
    const bool isFind = g.focus == 1;
    switch (sym) {
        case XKB_KEY_Escape: closeFind(); return false;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter: if (isFind) findNext(!shift); else replaceOne(); return false;
        case XKB_KEY_Tab: if (g.replaceOpen) g.focus = isFind ? 2 : 1; return false;
        case XKB_KEY_Left: f.cur = fieldPrev(f.s, f.cur); return true;
        case XKB_KEY_Right: f.cur = fieldNext(f.s, f.cur); return true;
        case XKB_KEY_Home: f.cur = 0; return false;
        case XKB_KEY_End: f.cur = f.s.size(); return false;
        case XKB_KEY_BackSpace:
            if (f.cur > 0) { const size_t p = fieldPrev(f.s, f.cur); f.s.erase(p, f.cur - p); f.cur = p; if (isFind) findNext(true, true); }
            return true;
        case XKB_KEY_Delete:
            if (f.cur < f.s.size()) { f.s.erase(f.cur, fieldNext(f.s, f.cur) - f.cur); if (isFind) findNext(true, true); }
            return true;
        case XKB_KEY_v: case XKB_KEY_V:
            if (ctrl) { std::string p = pasteFromClipboard(); p.erase(std::remove(p.begin(), p.end(), '\n'), p.end()); f.s.insert(f.cur, p); f.cur += p.size(); if (isFind) findNext(true, true); return false; }
            break;
        default: break;
    }
    if (ctrl) return false;
    bool consumed;
    const std::string t = keyText(key, sym, consumed);
    if (t.empty() || f.s.size() > 400) return consumed;
    f.s.insert(f.cur, t);
    f.cur += t.size();
    if (isFind) findNext(true, true);
    return true;
}

void moveBy(t3::Pos to, bool extend) {
    t3::Doc& d = doc();
    d.setCursor(to, extend);
    ensureCursorVisible();
}

bool handleKey(uint32_t key) {
    if (!g.xkbState) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, key + 8);
    const bool ctrl = modActive(XKB_MOD_NAME_CTRL), shift = modActive(XKB_MOD_NAME_SHIFT), alt = modActive(XKB_MOD_NAME_ALT);
    if (g.dialog != Dlg::None) {
        if (sym == XKB_KEY_Escape) dialogAnswer(DlgCancel);
        else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) dialogAnswer(DlgSave);
        return false;
    }
    t3::Doc& d = doc();
    if (ctrl) {  // the shortcuts, in the text and in the find bar
        switch (sym) {
            case XKB_KEY_q: case XKB_KEY_Q: closeWindowChecked(); return false;
            case XKB_KEY_s: case XKB_KEY_S: if (shift) saveDocAs(g.active); else saveDoc(g.active); return false;
            case XKB_KEY_o: case XKB_KEY_O: chooseFiles(); return false;
            case XKB_KEY_n: case XKB_KEY_N: addEmptyDoc(); return false;
            case XKB_KEY_w: case XKB_KEY_W: requestCloseDoc(g.active); return false;
            case XKB_KEY_b: case XKB_KEY_B: g.sidebar = !g.sidebar; saveSettings(); g.dirty = true; return false;
            case XKB_KEY_f: case XKB_KEY_F: openFind(false); return false;
            case XKB_KEY_h: case XKB_KEY_H: openFind(true); return false;
            case XKB_KEY_Tab: case XKB_KEY_ISO_Left_Tab: switchTo((g.active + (shift ? -1 : 1) + static_cast<int>(g.docs.size())) % static_cast<int>(g.docs.size())); return false;
            case XKB_KEY_Prior: switchTo(std::max(0, g.active - 1)); return false;
            case XKB_KEY_Next: switchTo(std::min(static_cast<int>(g.docs.size()) - 1, g.active + 1)); return false;
            case XKB_KEY_plus: case XKB_KEY_equal: case XKB_KEY_KP_Add: g.fontPx = std::min(40, g.fontPx + 1); saveSettings(); g.dirty = true; return false;
            case XKB_KEY_minus: case XKB_KEY_KP_Subtract: g.fontPx = std::max(9, g.fontPx - 1); saveSettings(); g.dirty = true; return false;
            case XKB_KEY_0: case XKB_KEY_KP_0: g.fontPx = 14; saveSettings(); g.dirty = true; return false;
            default: break;
        }
    }
    if (g.focus != 0) return fieldKey(key, sym, ctrl, shift);
    if (sym == XKB_KEY_F3) { if (g.findOpen) findNext(!shift); else openFind(false); return false; }
    if (sym == XKB_KEY_Escape) { if (g.findOpen) closeFind(); else if (d.hasSelection()) { d.anchor = d.cur; g.dirty = true; } return false; }
    if (ctrl) {
        switch (sym) {
            case XKB_KEY_z: case XKB_KEY_Z:  // (Caps Lock makes the key give a capital Z: only the Shift key means redo)
                if (shift) d.redo(); else d.undo();
                ensureCursorVisible();
                return false;
            case XKB_KEY_y: case XKB_KEY_Y: d.redo(); ensureCursorVisible(); return false;
            case XKB_KEY_a: case XKB_KEY_A: d.selectAll(); g.dirty = true; return false;
            case XKB_KEY_c: case XKB_KEY_C: copyToClipboard(d.selectedText()); return false;
            case XKB_KEY_x: case XKB_KEY_X: if (d.hasSelection() && !d.readOnly) { copyToClipboard(d.selectedText()); d.deleteSelection(); ensureCursorVisible(); } return false;
            case XKB_KEY_v: case XKB_KEY_V: {
                std::string p = pasteFromClipboard();
                p.erase(std::remove(p.begin(), p.end(), '\r'), p.end());
                if (!p.empty() && !d.readOnly) { t3::validUtf8(p, &p); d.insertText(p); ensureCursorVisible(); }
                return false;
            }
            case XKB_KEY_slash: case XKB_KEY_KP_Divide: d.toggleComment(); ensureCursorVisible(); return false;
            case XKB_KEY_d: case XKB_KEY_D: {  // duplicate the line (or the selection)
                if (d.readOnly) return false;
                if (d.hasSelection()) { const std::string t = d.selectedText(); const t3::Pos e = d.selEnd(); d.replaceRange(e, e, t, true, 0); }
                else { const std::string& s = d.lines[static_cast<size_t>(d.cur.line)]; d.replaceRange({d.cur.line, static_cast<int>(s.size())}, {d.cur.line, static_cast<int>(s.size())}, "\n" + s, true, 0); d.cur.line++; d.anchor = d.cur; }
                ensureCursorVisible();
                return false;
            }
            case XKB_KEY_Home: moveBy({0, 0}, shift); return false;
            case XKB_KEY_End: moveBy(d.endPos(), shift); return false;
            case XKB_KEY_Left: moveBy(d.wordLeft(d.cur), shift); return true;
            case XKB_KEY_Right: moveBy(d.wordRight(d.cur), shift); return true;
            case XKB_KEY_BackSpace: d.backspace(true); ensureCursorVisible(); return true;
            case XKB_KEY_Delete: d.deleteForward(true); ensureCursorVisible(); return true;
            default: return false;
        }
    }
    if (alt) {
        if (sym == XKB_KEY_Up || sym == XKB_KEY_Down) {  // Alt+Up / Alt+Down move the line
            if (d.readOnly) return true;
            const int l = d.cur.line, to = l + (sym == XKB_KEY_Up ? -1 : 1);
            if (to < 0 || to >= d.lineCount()) return true;
            const std::string a = d.lines[static_cast<size_t>(l)], b = d.lines[static_cast<size_t>(to)];
            const int lo = std::min(l, to);
            const int col = d.cur.col;
            d.replaceRange({lo, 0}, {lo + 1, static_cast<int>(d.lines[static_cast<size_t>(lo + 1)].size())}, (l < to ? b + "\n" + a : a + "\n" + b), true, 0);
            d.cur = d.anchor = d.clampPos({to, col});
            ensureCursorVisible();
        }
        return true;
    }
    switch (sym) {
        case XKB_KEY_Left: if (d.hasSelection() && !shift) moveBy(d.selStart(), false); else moveBy(d.left(d.cur), shift); return true;
        case XKB_KEY_Right: if (d.hasSelection() && !shift) moveBy(d.selEnd(), false); else moveBy(d.right(d.cur), shift); return true;
        case XKB_KEY_Up: { const int keep = d.wantVis; const t3::Pos p = d.vertical(d.cur, -1); d.cur = d.clampPos(p); if (!shift) d.anchor = d.cur; d.wantVis = keep < 0 ? t3::visualColumn(d.lines[static_cast<size_t>(d.cur.line)], d.cur.col) : keep; ensureCursorVisible(); return true; }
        case XKB_KEY_Down: { const int keep = d.wantVis; const t3::Pos p = d.vertical(d.cur, 1); d.cur = d.clampPos(p); if (!shift) d.anchor = d.cur; d.wantVis = keep < 0 ? t3::visualColumn(d.lines[static_cast<size_t>(d.cur.line)], d.cur.col) : keep; ensureCursorVisible(); return true; }
        case XKB_KEY_Prior: case XKB_KEY_Next: {
            const int page = std::max(1, g.viewLines - 2) * (sym == XKB_KEY_Prior ? -1 : 1);
            const int keep = d.wantVis;
            const t3::Pos p = d.vertical(d.cur, page);
            d.cur = d.clampPos(p);
            if (!shift) d.anchor = d.cur;
            d.wantVis = keep < 0 ? t3::visualColumn(d.lines[static_cast<size_t>(d.cur.line)], d.cur.col) : keep;
            d.scrollY = std::clamp(d.scrollY + page * g.lineH, 0.0, g.scrollMaxY);
            ensureCursorVisible();
            return false;
        }
        case XKB_KEY_Home: case XKB_KEY_KP_Home: moveBy(d.homePos(d.cur), shift); return false;
        case XKB_KEY_End: case XKB_KEY_KP_End: moveBy({d.cur.line, static_cast<int>(d.lines[static_cast<size_t>(d.cur.line)].size())}, shift); return false;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter: d.newline(); ensureCursorVisible(); return true;
        case XKB_KEY_BackSpace: d.backspace(false); ensureCursorVisible(); return true;
        case XKB_KEY_Delete: d.deleteForward(false); ensureCursorVisible(); return true;
        case XKB_KEY_Tab: case XKB_KEY_ISO_Left_Tab:
            if (d.readOnly) return false;
            if (sym == XKB_KEY_ISO_Left_Tab || shift) d.indentLines(true);
            else if (d.hasSelection() && d.selStart().line != d.selEnd().line) d.indentLines(false);
            else { d.insertText(std::string(static_cast<size_t>(t3::kTabWidth - t3::visualColumn(d.lines[static_cast<size_t>(d.cur.line)], d.cur.col) % t3::kTabWidth), ' '), 1); }
            ensureCursorVisible();
            return true;
        default: break;
    }
    bool consumed;
    const std::string t = keyText(key, sym, consumed);
    if (t.empty()) return consumed;
    if (d.readOnly) { say(i18n::tr("This file is read-only")); return false; }
    d.insertText(t, 1);
    ensureCursorVisible();
    return true;
}

// ---- the mouse -------------------------------------------------------------------------------------------------------------------------
t3::Pos posAt(double x, double y) {
    t3::Doc& d = doc();
    const double offY = d.scrollY + g.band.over;
    const int line = std::clamp(static_cast<int>(std::floor((y - g.textRect.y + offY) / g.lineH)), 0, d.lineCount() - 1);
    const double vx = x - (g.textRect.x + 6) + d.scrollX;
    const int vis = std::max(0, static_cast<int>(std::lround(vx / g.charW)));
    return {line, t3::colFromVisual(d.lines[static_cast<size_t>(line)], vis)};
}

void updateCursor();
void setHoverImpl() {
    int id = None, arg = 0;
    if (g.inside) for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it) if (it->r.contains(g.px, g.py)) { id = it->id; arg = it->arg; break; }
    if (id != g.hoverId || arg != g.hoverArg) { g.hoverId = id; g.hoverArg = arg; g.dirty = true; }
}
void setHover() { setHoverImpl(); updateCursor(); }

void updateCursor() {
    if (!g.cursorMgr || !g.pointer || !g.inside) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    int shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    switch (g.hoverId) {
        case TextArea: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT; break;
        case FindField: case ReplaceField: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT; break;
        case None: case Gutter: case Scrim: case Scrollbar: break;
        default: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER; break;
    }
    if (shape == g.cursorShape) return;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}

void dragScrollTo(double py) {
    const double travel = g.scrollTrack.h - g.thumbH;
    if (travel <= 0 || g.scrollMaxY <= 0) return;
    const double top = std::clamp(py - g.dragScrollOffset, g.scrollTrack.y, g.scrollTrack.y + travel);
    doc().scrollY = std::clamp((top - g.scrollTrack.y) / travel * g.scrollMaxY, 0.0, g.scrollMaxY);
    g.band.reset();
    g.dirty = true;
}
void onMotion() {
    if (g.dragScroll) { dragScrollTo(g.py); return; }
    if (g.selecting) {
        t3::Doc& d = doc();
        if (g.clickCount >= 3) { const t3::Pos p = posAt(g.px, g.py); const int a = d.anchor.line; if (p.line >= a) { d.anchor = {a, 0}; d.cur = p.line + 1 < d.lineCount() ? t3::Pos{p.line + 1, 0} : t3::Pos{p.line, static_cast<int>(d.lines[static_cast<size_t>(p.line)].size())}; } else { d.cur = {p.line, 0}; d.anchor = {a + 1 < d.lineCount() ? a + 1 : a, 0}; } }
        else d.cur = posAt(g.px, g.py);
        d.wantVis = -1;
        g.caretReset = Clock::now();
        g.dirty = true;
        return;
    }
    setHover();
}
void onRelease() { if (g.selecting) { g.selecting = false; doc().breakUndoGroup(); } if (g.dragScroll) { g.dragScroll = false; g.dirty = true; } }

void onClick(uint32_t button) {
    setHover();
    if (g.dialog != Dlg::None) {
        if (button != 0x110) return;
        if (g.hoverId == DlgSave || g.hoverId == DlgDiscard || g.hoverId == DlgCancel) dialogAnswer(g.hoverId);
        else if (g.hoverId == Scrim && g.hoverArg == 0) dialogAnswer(DlgCancel);
        return;
    }
    if (button != 0x110) return;
    if (g.hoverId != FindField && g.hoverId != ReplaceField && g.focus != 0 && !(g.hoverId == Scrim && g.hoverArg == 2)) { g.focus = 0; g.dirty = true; }
    t3::Doc& d = doc();
    switch (g.hoverId) {
        case BtnSidebar: g.sidebar = !g.sidebar; saveSettings(); g.dirty = true; break;
        case BtnNew: addEmptyDoc(); break;
        case BtnOpen: chooseFiles(); break;
        case BtnSave: saveDoc(g.active); break;
        case BtnFind: if (g.findOpen) closeFind(); else openFind(false); break;
        case BtnClose: closeWindowChecked(); break;
        case TabItem: switchTo(g.hoverArg); break;
        case TabClose: requestCloseDoc(g.hoverArg); break;
        case SideDoc: switchTo(g.hoverArg); break;
        case SideDocClose: requestCloseDoc(g.hoverArg); break;
        case SideUp: g.sideDir = fs::path(g.sideDir).parent_path().string(); readSideDir(); break;
        case SideFile:
            if (g.hoverArg >= 0 && g.hoverArg < static_cast<int>(g.sideEntries.size())) {
                const SideEntry e = g.sideEntries[static_cast<size_t>(g.hoverArg)];
                if (e.dir) { g.sideDir = e.path; readSideDir(); } else openPath(e.path);
            }
            break;
        case FindField: g.focus = 1; g.find.cur = g.find.s.size(); g.dirty = true; break;
        case ReplaceField: g.focus = 2; g.replace.cur = g.replace.s.size(); g.dirty = true; break;
        case FindPrev: findNext(false); break;
        case FindNext: findNext(true); break;
        case FindCase: g.matchCase = !g.matchCase; g.dirty = true; break;
        case FindClose: closeFind(); break;
        case ReplaceOne: replaceOne(); break;
        case ReplaceAllBtn: replaceEverything(); break;
        case Scrollbar: g.dragScroll = true; g.dragScrollOffset = g.py - g.thumbTop; g.dirty = true; break;
        case Gutter: {
            const t3::Pos p = posAt(g.textRect.x + 10, g.py);
            d.selectLine(p.line);
            g.selecting = true;
            g.clickCount = 3;
            g.dirty = true;
            break;
        }
        case TextArea: {
            const t3::Pos p = posAt(g.px, g.py);
            const auto now = Clock::now();
            const bool again = now - g.lastClick < std::chrono::milliseconds(420) && p.line == g.lastClickLine && std::abs(p.col - g.lastClickCol) <= 1;
            g.clickCount = again ? g.clickCount % 3 + 1 : 1;
            g.lastClick = now;
            g.lastClickLine = p.line;
            g.lastClickCol = p.col;
            if (modActive(XKB_MOD_NAME_SHIFT)) d.setCursor(p, true);
            else if (g.clickCount == 2) d.selectWordAt(p);
            else if (g.clickCount == 3) d.selectLine(p.line);
            else d.setCursor(p, false);
            g.selecting = true;
            g.focus = 0;
            g.caretReset = Clock::now();
            g.dirty = true;
            break;
        }
        default: break;
    }
}
void onWheel(double v) {
    if (g.dialog != Dlg::None) return;
    if (g.px < g.sideRect.x + g.sideRect.w && g.sidebar && g.px >= g.sideRect.x && g.py > g.sideRect.y) { elasticWheel(g.sideBand, g.sidePos, g.sideMax, v * 3); g.dirty = true; return; }
    if (g.py < kPad + 56) { g.tabScroll += v * 3; g.dirty = true; return; }
    t3::Doc& d = doc();
    if (modActive(XKB_MOD_NAME_CTRL)) { g.fontPx = std::clamp(g.fontPx + (v < 0 ? 1 : -1), 9, 40); saveSettings(); g.dirty = true; return; }
    if (modActive(XKB_MOD_NAME_SHIFT)) { d.scrollX = std::clamp(d.scrollX + v * 4, 0.0, g.scrollMaxX); g.dirty = true; return; }
    elasticWheel(g.band, d.scrollY, g.scrollMaxY, v * g.lineH / 10 * 3);
    g.dirty = true;
}

// ---- Wayland ---------------------------------------------------------------------------------------------------------------------------
void onFrameDone(void*, wl_callback* cb, uint32_t) { wl_callback_destroy(cb); g.frameCallback = nullptr; }
const wl_callback_listener kFrameListener = {onFrameDone};
void onBufferRelease(void* data, wl_buffer*) { static_cast<Buffer*>(data)->busy = false; }
const wl_buffer_listener kBufferListener = {onBufferRelease};

Buffer* acquireBuffer(int width, int height, double scale) {
    Buffer* spare = nullptr;
    for (auto& slot : g.buffers) {
        if (slot.busy) continue;
        if (slot.buffer && slot.frame.lw == width && slot.frame.lh == height && slot.frame.scale == scale) return &slot;
        if (!spare) spare = &slot;
    }
    if (!spare) return nullptr;
    spare->clear();
    auto& frame = spare->frame;
    if (!hidpi::begin(frame, width, height, scale, "end4pC-t3xt")) return nullptr;
    wl_shm_pool* pool = wl_shm_create_pool(g.shm, frame.fd, static_cast<int32_t>(frame.size));
    if (pool) {
        spare->buffer = wl_shm_pool_create_buffer(pool, 0, frame.pw, frame.ph, frame.stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
    }
    close(frame.fd);
    frame.fd = -1;
    if (!spare->buffer) { spare->clear(); return nullptr; }
    wl_buffer_add_listener(spare->buffer, &kBufferListener, spare);
    return spare;
}

void render() {
    if (!g.configured || !g.surface || g.frameCallback) return;
    Buffer* slot = acquireBuffer(g.width, g.height, g.scale.factor());
    if (!slot) return;
    g_scale = g.scale.factor();
    hidpi::Frame& f = slot->frame;
    cairo_t* cr = mkcr(f.surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    drawWindow(cr, g.width, g.height);
    cairo_destroy(cr);
    cairo_surface_flush(f.surface);
    g.frameCallback = wl_surface_frame(g.surface);
    if (g.frameCallback) wl_callback_add_listener(g.frameCallback, &kFrameListener, nullptr);
    slot->busy = true;
    hidpi::present(g.surface, slot->buffer, f, g.scale);
    g.dirty = false;
    setHover();
}

void onWmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmListener = {onWmPing};
void onXdgConfigure(void*, xdg_surface* s, uint32_t serial) { xdg_surface_ack_configure(s, serial); g.configured = true; g.dirty = true; }
const xdg_surface_listener kXdgSurfaceListener = {onXdgConfigure};
void onTopConfigure(void*, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
    if (w > 0 && h > 0) { g.width = std::max(kMinW, w); g.height = std::max(kMinH, h); }
    g.dirty = true;
}
void onTopClose(void*, xdg_toplevel*) { closeWindowChecked(); }
const xdg_toplevel_listener kToplevelListener = {onTopConfigure, onTopClose, nullptr, nullptr};

void onPEnter(void*, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) { g.enterSerial = serial; g.cursorShape = -1; g.inside = true; g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); setHover(); }
void onPLeave(void*, wl_pointer*, uint32_t, wl_surface*) { g.inside = false; setHover(); }
void onPMotion(void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) { g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); onMotion(); }
void onPButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) { if (state == WL_POINTER_BUTTON_STATE_PRESSED) onClick(button); else if (button == 0x110) onRelease(); }
void onPAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) { if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) onWheel(wl_fixed_to_double(value)); }
const wl_pointer_listener kPointerListener = {onPEnter, onPLeave, onPMotion, onPButton, onPAxis, [](void*, wl_pointer*) {}, [](void*, wl_pointer*, uint32_t) {},
                                              [](void*, wl_pointer*, uint32_t, uint32_t) {}, [](void*, wl_pointer*, uint32_t, int32_t) {}, nullptr, nullptr, nullptr};

void onKKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            if (!g.xkbCtx) g.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_keymap* km = xkb_keymap_new_from_string(g.xkbCtx, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (km) {
                if (g.xkbState) xkb_state_unref(g.xkbState);
                if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
                g.xkbMap = km;
                g.xkbState = xkb_state_new(km);
            }
        }
    }
    close(fd);
}
void onKKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) { if (key == g.repeatKey) g.repeatKey = 0; return; }
    if (handleKey(key) && g.kbRate > 0) { g.repeatKey = key; g.repeatAt = Clock::now() + std::chrono::milliseconds(g.kbDelay); }
    else g.repeatKey = 0;
}
void onKMods(void*, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) { if (g.xkbState) xkb_state_update_mask(g.xkbState, dep, lat, lock, 0, 0, group); }
void onKRepeat(void*, wl_keyboard*, int32_t rate, int32_t delay) { g.kbRate = rate; g.kbDelay = delay; }
const wl_keyboard_listener kKeyboardListener = {onKKeymap, [](void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}, [](void*, wl_keyboard*, uint32_t, wl_surface*) { g.repeatKey = 0; },
                                                onKKey, onKMods, onKRepeat};

void onSeatCaps(void*, wl_seat* seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g.pointer) { g.pointer = wl_seat_get_pointer(seat); wl_pointer_add_listener(g.pointer, &kPointerListener, nullptr); }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g.keyboard) { g.keyboard = wl_seat_get_keyboard(seat); wl_keyboard_add_listener(g.keyboard, &kKeyboardListener, nullptr); }
}
const wl_seat_listener kSeatListener = {onSeatCaps, [](void*, wl_seat*, const char*) {}};

void onGlobal(void*, wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
    if (!std::strcmp(iface, wl_compositor_interface.name)) g.compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    else if (!std::strcmp(iface, wl_shm_interface.name)) g.shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    else if (!std::strcmp(iface, wp_viewporter_interface.name) && !hidpi::disabled()) g.viewporter = static_cast<wp_viewporter*>(wl_registry_bind(reg, name, &wp_viewporter_interface, 1));
    else if (!std::strcmp(iface, wp_fractional_scale_manager_v1_interface.name) && !hidpi::disabled())
        g.fscaleMgr = static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1));
    else if (!std::strcmp(iface, wp_cursor_shape_manager_v1_interface.name)) g.cursorMgr = static_cast<wp_cursor_shape_manager_v1*>(wl_registry_bind(reg, name, &wp_cursor_shape_manager_v1_interface, 1));
    else if (!std::strcmp(iface, wl_seat_interface.name)) {
        g.seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(g.seat, &kSeatListener, nullptr);
    } else if (!std::strcmp(iface, xdg_wm_base_interface.name)) {
        g.wm = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(g.wm, &kWmListener, nullptr);
    }
}
const wl_registry_listener kRegistryListener = {onGlobal, [](void*, wl_registry*, uint32_t) {}};

bool connectWayland() {
    g.display = wl_display_connect(nullptr);
    if (!g.display) { std::fprintf(stderr, "end4pC-t3xt: no Wayland display\n"); return false; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    if (wl_display_roundtrip(g.display) < 0 || !g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-t3xt: missing Wayland globals\n"); return false; }
    wl_display_roundtrip(g.display);
    g.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(g.scale, g.surface, g.viewporter, g.fscaleMgr, [] { g.dirty = true; });
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, "T3xt");
    xdg_toplevel_set_app_id(g.toplevel, "end4pC-t3xt");
    xdg_toplevel_set_min_size(g.toplevel, kMinW, kMinH);
    wl_surface_commit(g.surface);
    return true;
}

void tick() {
    const auto now = Clock::now();
    {  // what the file dialogs answered
        std::vector<std::string> opened;
        std::string saved;
        {
            std::lock_guard<std::mutex> lock(g.chosenMutex);
            opened.swap(g.chosenOpen);
            saved.swap(g.chosenSave);
        }
        for (const std::string& p : opened) openPath(p);
        if (!saved.empty()) {
            if (saved == "\x01") { g_after = After::Nothing; g_afterDoc = -1; }
            else if (g_afterDoc >= 0 && g_afterDoc < static_cast<int>(g.docs.size())) {
                std::string error;
                t3::Doc& d = *g.docs[static_cast<size_t>(g_afterDoc)];
                if (d.save(saved, error)) { if (g_afterDoc == g.active) { g.sideDir.clear(); followActiveFile(); } afterSaved(); }
                else { say(fs::path(saved).filename().string() + ": " + error); g_after = After::Nothing; }
            }
            g.dirty = true;
        }
    }
    if (g.repeatKey && now >= g.repeatAt) {
        if (!handleKey(g.repeatKey)) g.repeatKey = 0;
        g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
    }
    if (mtimeOf(colorsPath()) != g_colorsStamp) { loadColors(); g.dirty = true; }
    if (g.band.step() | g.sideBand.step()) g.dirty = true;
    if (g.selecting && g.dialog == Dlg::None) {  // dragging near the top or bottom edge scrolls the text by itself
        const double edge = 28, speed = 16;
        const double above = g.textRect.y + edge - g.py, below = g.py - (g.textRect.y + g.textRect.h - edge);
        t3::Doc& d = doc();
        if (above > 0) d.scrollY = std::max(0.0, d.scrollY - speed * std::min(1.0, above / edge));
        else if (below > 0) d.scrollY = std::min(g.scrollMaxY, d.scrollY + speed * std::min(1.0, below / edge));
        if (above > 0 || below > 0) { d.cur = posAt(g.px, g.py); g.dirty = true; }
    }
    if (!g.toast.empty()) g.dirty = true;
    {  // the caret blinks (a key press keeps it on)
        const bool on = std::chrono::duration_cast<std::chrono::milliseconds>(now - g.caretReset).count() / 530 % 2 == 0;
        if (on != g.caretOn) { g.caretOn = on; g.dirty = true; }
    }
}

int pollTimeoutBase() {
    if (g.band.busy() || g.sideBand.busy() || g.selecting) return 16;
    if (!g.toast.empty()) return 100;
    return 140;
}

// A key held down repeats from the loop, so the loop must wake when the next repeat is due (otherwise the repeat only runs when something else wakes it).
int pollTimeout() {
    int t = pollTimeoutBase();
    if (g.repeatKey) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(g.repeatAt - Clock::now()).count();
        t = std::min<int>(t, static_cast<int>(std::max<long long>(1, left)));
    }
    return t;
}

void teardown() {
    if (g.cursorDev) wp_cursor_shape_device_v1_destroy(g.cursorDev);
    if (g.frameCallback) wl_callback_destroy(g.frameCallback);
    if (g.toplevel) xdg_toplevel_destroy(g.toplevel);
    if (g.xsurface) xdg_surface_destroy(g.xsurface);
    hidpi::detach(g.scale);
    if (g.surface) wl_surface_destroy(g.surface);
    for (auto& b : g.buffers) b.clear();
    if (g.display) { wl_display_flush(g.display); wl_display_disconnect(g.display); }
    if (g.composeState) xkb_compose_state_unref(g.composeState);
    if (g.composeTable) xkb_compose_table_unref(g.composeTable);
    if (g.xkbState) xkb_state_unref(g.xkbState);
    if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
    if (g.xkbCtx) xkb_context_unref(g.xkbCtx);
}

volatile sig_atomic_t g_term = 0;
void onTermSignal(int) { g_term = 1; }

int run(int argc, char** argv) {
    std::string lang;
    const std::string cfg = readFile(configPath());
    jsonValue(cfg, "language", lang);
    std::string mat, style, scale;
    matsym::assetsDir() = assetsDir();
    matsym::enabled() = jsonValue(cfg, "materialIcons", mat) && mat == "1";
    if (jsonValue(cfg, "iconScale", scale) && !scale.empty()) g_iconScale = std::clamp(std::atoi(scale.c_str()), 60, 150);
    if (jsonValue(cfg, "iconStyle", style) && !style.empty()) g_iconStyle = style;
    i18n::load(lang, assetsDir(), dataDir() + "/i18n");
    applyShellFont(fontFamilyFromConfig(dataDir() + "/config.json"), fontWeightFromConfig(dataDir() + "/config.json", "fontWeightText", 450), fontWeightFromConfig(dataDir() + "/config.json", "fontWeightTitle", 550));
    loadColors();
    loadSettings();
    if (handOffToRunning(argc, argv)) return 0;  // already open: it gets the files
    if (!connectWayland()) return 1;
    listenForHandOffs();
    // dead keys and compose sequences, from the locale of the session
    g.xkbCtx = g.xkbCtx ? g.xkbCtx : xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    {
        const char* loc = std::getenv("LC_ALL");
        if (!loc || !*loc) loc = std::getenv("LC_CTYPE");
        if (!loc || !*loc) loc = std::getenv("LANG");
        g.composeTable = xkb_compose_table_new_from_locale(g.xkbCtx, loc && *loc ? loc : "C", XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (g.composeTable) g.composeState = xkb_compose_state_new(g.composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
    }
    cairo_surface_t* dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* dcr = mkcr(dummy);
    g_measure = pango_cairo_create_layout(dcr);
    measureFont();

    int wake[2];
    if (pipe2(wake, O_CLOEXEC | O_NONBLOCK) < 0) return 1;
    g_wakeFd = wake[1];
    g.docs.push_back(std::make_unique<t3::Doc>());
    g.sideDir = homeDir();
    if (g.restoreSession) restoreSession();
    for (int i = 1; i < argc; i++) openPath(argv[i]);
    if (g.docs.size() == 1 && doc().path.empty()) readSideDir(); else followActiveFile();
    if (g.sideEntries.empty() && !g.sideDir.empty()) readSideDir();

    std::signal(SIGTERM, onTermSignal);
    std::signal(SIGINT, onTermSignal);
    std::signal(SIGHUP, onTermSignal);
    std::signal(SIGPIPE, SIG_IGN);
    while (g.running && !g_term) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);
        pollfd fds[] = {{wl_display_get_fd(g.display), POLLIN, 0}, {wake[0], POLLIN, 0}, {g_listenFd, POLLIN, 0}};
        if (poll(fds, 3, pollTimeout()) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & POLLIN) {
            char junk[64];
            while (read(wake[0], junk, sizeof junk) > 0) {}
            g.dirty = true;
        }
        if (fds[2].revents & POLLIN) acceptHandOffs();
        tick();
        if (g.dirty) render();
    }
    saveSession();
    if (g_listenFd >= 0) { close(g_listenFd); unlink(socketPath().c_str()); }
    teardown();
    g_object_unref(g_measure);
    cairo_destroy(dcr);
    cairo_surface_destroy(dummy);
    return 0;
}
