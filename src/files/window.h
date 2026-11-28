// What Fil3s does and its Wayland window: navigation, selection, the operations, the menu, the keys, the loop.
// Included once by fil3s.cpp, inside its anonymous namespace (after ui.h).
#pragma once

// ---- the folder ------------------------------------------------------------------------------------------------------------------------

void watchFolder() {
    if (pane().inotifyFd < 0) return;
    if (pane().watch >= 0) { inotify_rm_watch(pane().inotifyFd, pane().watch); pane().watch = -1; }
    pane().watch = inotify_add_watch(pane().inotifyFd, pane().cwd.c_str(), IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);
}
void reload(bool keepScroll) {
    pane().generation++;
    pane().loading = pane().entries.empty() || !keepScroll;
    pane().keepScroll = keepScroll;
    pane().lister->request(pane().cwd, pane().generation);
    g.dirty = true;
}
void navigate(const std::string& path, bool record = true) {
    std::error_code ec;
    const std::string target = fs::weakly_canonical(path, ec).string();
    if (ec || !fs::is_directory(target, ec)) { g.toast = i18n::tr("That folder cannot be opened"); g.toastAt = Clock::now(); g.dirty = true; return; }
    if (record && !pane().cwd.empty() && target != pane().cwd) { pane().back.push_back(pane().cwd); pane().forward.clear(); if (pane().back.size() > 200) pane().back.erase(pane().back.begin()); }
    if (pane().cwd != target) {
        pane().space = {};
        pane().spaceAt = {};
    }
    pane().cwd = target;
    pane().selectionDirty = true;
    g.focus = 0;
    pane().lastClickIndex = -1;
    pane().scroll = pane().scrollMax = 0;
    pane().band.reset();
    pane().reloadAt = {};
    pane().selectAfter.clear();
    pane().entries.clear();
    pane().view.clear();
    pane().selected.clear();
    pane().cursor = pane().anchor = -1;
    pane().search = {};
    pane().loadError.clear();
    if (!g.split) g.thumbs.forgetPending();
    pane().waitingThumbs.clear();
    pane().fadingThumbs.clear();
    watchFolder();
    reload(false);
}
void goBack() { if (pane().back.empty()) return; const std::string to = pane().back.back(); pane().back.pop_back(); pane().forward.push_back(pane().cwd); navigate(to, false); }
void goForward() { if (pane().forward.empty()) return; const std::string to = pane().forward.back(); pane().forward.pop_back(); pane().back.push_back(pane().cwd); navigate(to, false); }
void goUp() { if (pane().cwd != "/" && !inTrash()) navigate(fs::path(pane().cwd).parent_path().string()); }

void rebuildView() {
    pane().view.clear();
    pane().visibleFolders = 0;
    const std::string needle = lowerCase(pane().search.s);
    for (size_t i = 0; i < pane().entries.size(); i++) {
        const Entry& e = pane().entries[i];
        if (e.hidden && !g.showHidden && needle.empty()) continue;
        if (!needle.empty() && e.lower.find(needle) == std::string::npos) continue;
        pane().view.push_back(static_cast<int>(i));
        if (e.dir) ++pane().visibleFolders;
    }
    pane().cursor = std::min(pane().cursor, static_cast<int>(pane().view.size()) - 1);
    pane().viewDirty = false;
    pane().selectionDirty = true;
    g.dirty = true;
}
void applyListing(Listing& l) {
    if (l.dir != pane().cwd) return;
    pane().entries = std::move(l.entries);
    sortEntries(pane().entries, pane().sort, pane().descending, pane().foldersFirst, pane().hiddenLast);
    pane().loading = false;
    pane().loadError = l.error;
    std::set<std::string> present;
    for (const Entry& e : pane().entries) present.insert(e.path);
    for (auto it = pane().selected.begin(); it != pane().selected.end();) it = present.count(*it) ? std::next(it) : pane().selected.erase(it);
    if (!pane().selectAfter.empty()) {
        pane().selected.clear();
        for (const std::string& p : pane().selectAfter) if (present.count(p)) pane().selected.insert(p);
        pane().selectAfter.clear();
    }
    if (!pane().keepScroll) { pane().scroll = 0; pane().band.reset(); }
    rebuildView();
}
void resort() {
    sortEntries(pane().entries, pane().sort, pane().descending, pane().foldersFirst, pane().hiddenLast);
    rebuildView();
}

void toggleSplit() {
    g.focus = 0;
    pane().lasso = pane().dragScroll = false;
    if (g.split) {
        Pane& other = g.panes[1 - g.activePane];
        if (other.watch >= 0) inotify_rm_watch(other.inotifyFd, other.watch);
        other.watch = -1;
        other.reloadAt = {};
        g.split = false;
    } else {
        const std::string path = pane().cwd;
        const ViewMode mode = pane().mode;
        const Sort sort = pane().sort;
        const bool descending = pane().descending;
        const bool foldersFirst = pane().foldersFirst, hiddenLast = pane().hiddenLast;
        g.split = true;
        g.hitPane = 1 - g.activePane;
        pane().back.clear();
        pane().forward.clear();
        pane().mode = mode;
        pane().sort = sort;
        pane().descending = descending;
        pane().foldersFirst = foldersFirst;
        pane().hiddenLast = hiddenLast;
        navigate(path, false);
        g.hitPane = -1;
    }
    g.hits.clear();
    g.dirty = true;
}

void focusPane(int index) {
    if (!g.split || index < 0 || index == g.activePane) return;
    pane().lastClickIndex = -1;
    g.activePane = index;
    pane().lastClickIndex = -1;
    g.focus = 0;
    g.dirty = true;
}

void setView(ViewMode mode) {
    if (pane().mode == mode) return;
    pane().mode = mode;
    pane().scroll = pane().scrollMax = 0;
    pane().band.reset();
    pane().lastClickIndex = -1;
    pane().dragScroll = pane().lasso = false;
    g.dirty = true;
}

void setSort(Sort by) {
    if (pane().sort == by) pane().descending = !pane().descending;
    else {
        pane().sort = by;
        pane().descending = by == Sort::Size || by == Sort::Date || by == Sort::Accessed;
    }
    resort();
}

// ---- selection -------------------------------------------------------------------------------------------------------------------------
void selectOnly(int i) {
    pane().selectionDirty = true;
    pane().selected.clear();
    if (i >= 0 && i < static_cast<int>(pane().view.size())) pane().selected.insert(entryAt(i).path);
    pane().cursor = pane().anchor = i;
    g.dirty = true;
}
void selectRange(int to) {
    pane().selectionDirty = true;
    if (pane().anchor < 0) pane().anchor = 0;
    pane().selected.clear();
    for (int i = std::min(pane().anchor, to); i <= std::max(pane().anchor, to) && i < static_cast<int>(pane().view.size()); i++) pane().selected.insert(entryAt(i).path);
    pane().cursor = to;
    g.dirty = true;
}
void toggleOne(int i) {
    pane().selectionDirty = true;
    const std::string& p = entryAt(i).path;
    if (!pane().selected.erase(p)) pane().selected.insert(p);
    pane().cursor = pane().anchor = i;
    g.dirty = true;
}
void selectAll() {
    pane().selectionDirty = true;
    for (int i : pane().view) pane().selected.insert(pane().entries[static_cast<size_t>(i)].path);
    g.dirty = true;
}
std::vector<const Entry*> selectedEntries() {
    std::vector<const Entry*> out;
    for (int i : pane().view) { const Entry& e = pane().entries[static_cast<size_t>(i)]; if (pane().selected.count(e.path)) out.push_back(&e); }
    return out;
}
std::vector<std::string> selectedPaths() {
    std::vector<std::string> out;
    for (const Entry* e : selectedEntries()) out.push_back(e->path);
    return out;
}
void scrollToCursor() {
    if (pane().cursor < 0 || pane().cellH <= 0 || pane().columns <= 0) return;
    const Rect item = itemRect(pane().cursor);
    const double header = pane().mode == ViewMode::Details ? 34 : 0;
    const double start = horizontal() ? item.x : item.y - header;
    const double end = start + (horizontal() ? item.w : item.h);
    const double viewport = horizontal() ? pane().content.w : pane().content.h - header;
    if (start < pane().scroll) pane().scroll = start;
    else if (end > pane().scroll + viewport) pane().scroll = end - viewport;
    pane().scroll = std::clamp(pane().scroll, 0.0, pane().scrollMax);
    g.dirty = true;
}

// ---- operations ------------------------------------------------------------------------------------------------------------------------
void say(const std::string& msg) { g.toast = msg; g.toastAt = Clock::now(); g.dirty = true; }

// A name typed in a dialog: a single path component (no slash, not "." or "..", no control characters, not longer than a file name can be).
bool validName(const std::string& n) {
    if (n.empty() || n.size() > 255 || n == "." || n == "..") return false;
    for (const unsigned char c : n) if (c == '/' || c < 0x20 || c == 0x7f) return false;
    return true;
}
void openEntry(int i) {
    const Entry& e = entryAt(i);
    if (e.dir) { navigate(e.path); return; }
    if (inTrash()) { say(i18n::tr("Restore it from the Trash to open it")); return; }
    if (!e.regular) { say(i18n::tr("That is not a regular file")); return; }
    launchDetached("xdg-open", e.path.c_str());
}
void openAddress(bool selectPath = true) {
    Pane& p = pane();
    g.focus = 3;
    p.address = {p.cwd, p.cwd.size()};
    p.addressAnchor = selectPath ? 0 : p.address.cur;
    p.addressScroll = 0;
    g.caretOn = true;
    g.caretReset = Clock::now();
    g.dirty = true;
}

fs::path enteredPath(const std::string& input, const std::string& cwd) {
    if (input.empty()) return {};
    if (input.starts_with("file:")) {
        gchar* host = nullptr;
        gchar* file = g_filename_from_uri(input.c_str(), &host, nullptr);
        const fs::path path = file && (!host || g_ascii_strcasecmp(host, "localhost") == 0) ? fs::path(file) : fs::path{};
        g_free(host);
        g_free(file);
        return path;
    }
    if (input == "~") return homeDir();
    if (input.starts_with("~/")) return homeDir() + input.substr(1);
    return fs::path(cwd) / fs::path(input);
}

void submitAddress() {
    const auto path = enteredPath(pane().address.s, pane().cwd);
    std::error_code ec;
    const auto type = path.empty() ? fs::file_status{} : fs::status(path, ec);
    if (!ec && fs::is_directory(type)) navigate(path.string());
    else if (!ec && fs::is_regular_file(type)) {
        launchDetached("xdg-open", path.c_str());
        g.focus = 0;
        g.dirty = true;
    } else say(i18n::tr("That location cannot be opened"));
}

void toggleHidden() {
    const bool show = !g.showHidden;
    if (!rememberHidden(show)) say(i18n::tr("Could not save the hidden files preference"));
    g.showHidden = show;
    rebuildView();
    if (g.split) g.panes[1 - g.activePane].viewDirty = true;
}

void copyText(const std::vector<std::string>& lines) {
    if (lines.empty()) return;
    std::string text;
    for (const std::string& line : lines) {
        if (!text.empty()) text += '\n';
        text += line;
    }
    g.worker.submit({Op::CopyText, {}, std::move(text), {}});
}

std::string actionFolder(bool selectedFolder) {
    if (!selectedFolder) return pane().cwd;
    const auto sel = selectedEntries();
    return sel.size() == 1 && sel.front()->dir ? sel.front()->path : std::string{};
}

void copySelection(bool cut) {
    g.clip = selectedPaths();
    g.clipCut = cut;
    if (!g.clip.empty()) say(std::to_string(g.clip.size()) + " · " + (cut ? i18n::tr("Ready to move") : i18n::tr("Ready to copy")));
}
void pasteHere() {
    if (g.clip.empty() || inTrash()) return;
    g.worker.submit({g.clipCut ? Op::Move : Op::Copy, g.clip, pane().cwd, {}});
    if (g.clipCut) g.clip.clear();
}
void trashSelection() {
    const auto paths = selectedPaths();
    if (paths.empty()) return;
    if (inTrash()) { g.dialog = Dlg::ConfirmDelete; g.dirty = true; return; }
    g.worker.submit({Op::Trash, paths, "", {}});
}
void askDeleteForever() { if (!pane().selected.empty()) { g.dialog = Dlg::ConfirmDelete; g.dirty = true; } }
void restoreSelection() { const auto p = selectedPaths(); if (!p.empty()) g.worker.submit({Op::Restore, p, "", {}}); }
void emptyTrash() {
    std::vector<std::string> all;
    std::error_code ec;
    for (fs::directory_iterator it(trashFiles(), ec), end; !ec && it != end; it.increment(ec)) all.push_back(it->path().string());
    if (!all.empty()) g.worker.submit({Op::Delete, all, "", {}});
}
void openDialog(Dlg d, const std::string& initial = "", const std::string& target = "") {
    g.dialog = d;
    g.dialogField = {initial, initial.size()};
    g.dialogTarget = target;
    g.focus = d == Dlg::ConfirmDelete ? 0 : 2;
    g.caretReset = Clock::now();
    g.dirty = true;
}
void startRename() {
    const auto sel = selectedEntries();
    if (sel.size() != 1 || inTrash()) return;
    const std::string& n = sel[0]->name;
    const size_t dot = n.rfind('.');
    openDialog(Dlg::Rename, n, sel[0]->path);
    if (dot != std::string::npos && dot > 0 && !sel[0]->dir) g.dialogField.cur = dot;  // the caret before the extension
}
void closeDialog() { g.dialog = Dlg::None; g.focus = 0; g.dirty = true; }
void commitDialog() {
    if (g.dialog == Dlg::ConfirmDelete) {
        const auto paths = selectedPaths();
        closeDialog();
        if (!paths.empty()) g.worker.submit({Op::Delete, paths, "", {}});
        return;
    }
    const std::string name = g.dialogField.s;
    if (!validName(name)) { say(i18n::tr("That name is not valid")); return; }
    std::error_code ec;
    const Dlg kind = g.dialog;
    if (kind == Dlg::Rename) {
        const std::string to = (fs::path(g.dialogTarget).parent_path() / name).string();
        if (to == g.dialogTarget) { closeDialog(); return; }
        if (fs::exists(fs::symlink_status(to, ec))) { say(i18n::tr("A file with that name already exists")); return; }
        std::string error;
        if (!moveAny(g.dialogTarget, to, error)) { say(error); return; }
        pane().selectAfter = {to};
    } else {
        const std::string to = (fs::path(pane().cwd) / name).string();
        if (fs::exists(fs::symlink_status(to, ec))) { say(i18n::tr("A file with that name already exists")); return; }
        if (kind == Dlg::NewFolder) fs::create_directory(to, ec);
        else { const int fd = open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644); if (fd >= 0) close(fd); else ec = std::error_code(errno, std::generic_category()); }
        if (ec) { say(ec.message()); return; }
        pane().selectAfter = {to};
    }
    closeDialog();
    reload(true);
}

// The conversions and archives (see convert.h): one click, the new file appears next to the original.
void submitCommands(std::vector<Cmd> cmds) {
    if (cmds.empty()) return;
    g.worker.submit({Op::Command, {}, "", std::move(cmds)});
}
void convertSelection(int targetIndex) {
    const Target& t = targets()[static_cast<size_t>(targetIndex)];
    std::vector<Cmd> cmds;
    for (const Entry* e : selectedEntries()) if (e->regular && offers(e->kind, t, extensionOf(e->name))) cmds.push_back(convertCommand(*e, t));
    submitCommands(std::move(cmds));
}
void compressSelection(int formatIndex) {
    const auto sel = selectedEntries();
    if (sel.empty()) return;
    submitCommands({archiveCommand(sel, archiveFormats()[static_cast<size_t>(formatIndex)], pane().cwd)});
}
void extractSelection(bool intoFolder) {
    std::vector<Cmd> cmds;
    for (const Entry* e : selectedEntries()) if (e->kind == Kind::Archive && e->regular) cmds.push_back(extractCommand(*e, intoFolder));
    submitCommands(std::move(cmds));
}

// ---- the places ------------------------------------------------------------------------------------------------------------------------
bool placeExists(const std::string& path) {
    for (const Place& p : g.places) if (p.path == path) return true;
    return false;
}
void reindexPlaces() { for (size_t i = 0; i < g.places.size(); i++) g.places[i].user = static_cast<int>(i); }
void addPlace(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_directory(path, ec)) { say(i18n::tr("That folder cannot be opened")); return; }
    if (placeExists(path)) { say(i18n::tr("Already in Places")); return; }
    g.places.push_back(makePlace(path));
    reindexPlaces();
    savePlaces(g.places);
    g.dirty = true;
}
void removePlace(int i) {
    if (i < 0 || i >= static_cast<int>(g.places.size())) return;
    g.places.erase(g.places.begin() + i);
    reindexPlaces();
    savePlaces(g.places);
    g.dirty = true;
}
void movePlace(int from, int to) {
    const int n = static_cast<int>(g.places.size());
    if (from < 0 || from >= n) return;
    to = std::clamp(to, 0, n - 1);
    if (from == to) return;
    Place moved = g.places[static_cast<size_t>(from)];
    g.places.erase(g.places.begin() + from);
    g.places.insert(g.places.begin() + to, std::move(moved));
    reindexPlaces();
    savePlaces(g.places);
    g.dirty = true;
}
void updateDrop() {
    const int n = static_cast<int>(g.places.size());
    if (n == 0) return;
    const int to = std::clamp(static_cast<int>((g.py - g.placeRowsY0) / g.placeStep), 0, n - 1);
    if (to != g.dropIndex) { g.dropIndex = to; g.dirty = true; }
}
void cancelPlacePress() { g.pressedPlace = -1; g.placeDrag = false; g.dirty = true; }

// ---- the menu --------------------------------------------------------------------------------------------------------------------------
MenuItem item(const std::string& label, const char* ic, Act act, int arg = 0, bool enabled = true) {
    MenuItem m;
    m.label = label; m.icon = ic; m.act = act; m.arg = arg; m.enabled = enabled;
    return m;
}
MenuItem separatorItem() { MenuItem m; m.separator = true; return m; }

void addFolderActions(std::vector<MenuItem>& menu, bool selectedFolder) {
    const int arg = selectedFolder ? 1 : 0;
    menu.push_back(item(i18n::tr("Open in New Window"), "window-frame", Act::NewWindow, arg));
    menu.push_back(item(i18n::tr("Open in Terminal"), "code-file", Act::Terminal, arg, haveTool(g.terminal)));
}

using Application = std::shared_ptr<GAppInfo>;
struct FileApplications {
    Application preferred;
    std::vector<Application> alternatives;
};

FileApplications fileApplications(const Entry& e) {
    FileApplications result;
    if (!e.regular) return result;
    GFile* file = g_file_new_for_path(e.path.c_str());
    GFileInfo* info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
    g_object_unref(file);
    if (info) {
        if (const char* type = g_file_info_get_content_type(info)) {
            if (GAppInfo* preferred = g_app_info_get_default_for_type(type, false))
                result.preferred = {preferred, g_object_unref};
            GList* apps = g_app_info_get_all_for_type(type);
            for (GList* entry = apps; entry; entry = entry->next) {
                auto* app = G_APP_INFO(entry->data);
                if (g_app_info_should_show(app) && (!result.preferred || !g_app_info_equal(app, result.preferred.get())))
                    result.alternatives.emplace_back(G_APP_INFO(g_object_ref(app)), g_object_unref);
            }
            g_list_free_full(apps, g_object_unref);
        }
        g_object_unref(info);
    }
    return result;
}

MenuItem openWithRow(const std::string& label, const char* icon, const Application& app, const std::string& path) {
    auto row = item(label, icon, Act::OpenWith);
    row.application = app;
    row.target = path;
    return row;
}

void showOtherApplications(const std::string& path) {
    if (g.menuSub < 0 || g.menuSub >= static_cast<int>(g.menu.size())) return;
    std::vector<MenuItem> choices;
    GList* installed = g_app_info_get_all();
    for (GList* entry = installed; entry; entry = entry->next) {
        auto* app = G_APP_INFO(entry->data);
        if (!g_app_info_should_show(app) || (!g_app_info_supports_files(app) && !g_app_info_supports_uris(app))) continue;
        Application application{G_APP_INFO(g_object_ref(app)), g_object_unref};
        choices.push_back(openWithRow(g_app_info_get_display_name(app), "", application, path));
    }
    g_list_free_full(installed, g_object_unref);
    std::stable_sort(choices.begin(), choices.end(), [](const MenuItem& a, const MenuItem& b) {
        return g_utf8_collate(a.label.c_str(), b.label.c_str()) < 0;
    });
    if (choices.empty()) choices.push_back(item(i18n::tr("No applications available"), "", Act::None, 0, false));
    g.menu[static_cast<size_t>(g.menuSub)].children = std::move(choices);
    g.menuOffset = 0;
    g.subPending = -2;
    g.dirty = true;
}

void addOpenActions(const Entry& e) {
    if (e.dir) {
        g.menu.push_back(item(i18n::tr("Open folder"), "folder-open", Act::Open));
        return;
    }
    if (e.executable) {
        MenuItem run = item(i18n::tr("Run Executable"), "play", Act::RunExecutable);
        run.target = e.path;
        g.menu.push_back(std::move(run));
    }
    const auto apps = fileApplications(e);
    if (apps.preferred) {
        g.menu.push_back(openWithRow(std::string(i18n::tr("Open with")) + " " + g_app_info_get_display_name(apps.preferred.get()),
                                     "file", apps.preferred, e.path));
    }
    if (!e.regular) return;
    MenuItem other = item(i18n::tr("Open With…"), "window-frame", Act::None);
    constexpr size_t kQuickApplications = 4;
    for (size_t i = 0; i < std::min(kQuickApplications, apps.alternatives.size()); ++i) {
        const auto& app = apps.alternatives[i];
        other.children.push_back(openWithRow(g_app_info_get_display_name(app.get()), "", app, e.path));
    }
    if (!other.children.empty()) other.children.push_back(separatorItem());
    auto browse = item(i18n::tr("Other Applications…"), "widget", Act::OtherApplications);
    browse.target = e.path;
    other.children.push_back(std::move(browse));
    g.menu.push_back(std::move(other));
}

void launchApplication(GAppInfo* app, const std::string& path) {
    GFile* file = g_file_new_for_path(path.c_str());
    GList files{};
    files.data = file;
    GError* error = nullptr;
    if (!g_app_info_launch(app, &files, nullptr, &error)) {
        say(error ? error->message : i18n::tr("Could not open the file"));
        g_clear_error(&error);
    }
    g_object_unref(file);
}

void openViewMenu(bool sortMenu) {
    g.menu.clear();
    auto choice = [](const char* label, const char* icon, Act action, int arg, bool checked) {
        auto row = item(i18n::tr(label), icon, action, arg);
        row.checked = checked;
        g.menu.push_back(std::move(row));
    };
    if (sortMenu) {
        const char* labels[] = {"Name", "Size", "Modified", "Type", "Extension", "Last Accessed"};
        for (int i = 0; i < static_cast<int>(std::size(labels)); ++i)
            choice(labels[i], "", Act::SetSort, i, i == static_cast<int>(pane().sort));
        g.menu.push_back(separatorItem());
        choice("Ascending", "alt-arrow-up", Act::SortOrder, 0, !pane().descending);
        choice("Descending", "alt-arrow-down", Act::SortOrder, 1, pane().descending);
        g.menu.push_back(separatorItem());
        choice("Folders First", "folder", Act::FoldersFirst, 0, pane().foldersFirst);
        choice("Hidden Files Last", "eye-closed", Act::HiddenLast, 0, pane().hiddenLast);
    } else {
        choice("Icons", "widget", Act::SetView, static_cast<int>(ViewMode::Icons), pane().mode == ViewMode::Icons);
        choice("Details", "list", Act::SetView, static_cast<int>(ViewMode::Details), pane().mode == ViewMode::Details);
        choice("Compact", "widget-2", Act::SetView, static_cast<int>(ViewMode::Compact), pane().mode == ViewMode::Compact);
    }
    g.menuOpen = true;
    g.menuSub = -1; g.menuOffset = 0;
    g.subPending = -2;
    g.menuX = g.px;
    g.menuY = g.py;
    g.dirty = true;
}

void buildMenu(bool onItem) {
    g.menu.clear();
    g.menuSub = -1; g.menuOffset = 0;
    g.subPending = -2;
    const auto sel = selectedEntries();
    if (inTrash()) {
        if (onItem) {
            g.menu.push_back(item(i18n::tr("Restore"), "restart", Act::Restore));
            g.menu.push_back(item(i18n::tr("Delete forever"), "trash-bin-2", Act::Delete));
            g.menu.push_back(item(i18n::tr("Copy Full Path"), "link", Act::CopyPath));
            g.menu.push_back(separatorItem());
        } else g.menu.push_back(item(i18n::tr("Copy Folder Path"), "link", Act::CopyFolderPath));
        g.menu.push_back(item(i18n::tr("Empty the Trash"), "trash-bin-trash", Act::EmptyTrash, 0, !pane().entries.empty()));
        g.menu.push_back(item(i18n::tr("Select all"), "list-check", Act::SelectAll));
        return;
    }
    if (onItem && !sel.empty()) {
        const bool one = sel.size() == 1;
        if (one) { addOpenActions(*sel.front()); g.menu.push_back(separatorItem()); }
        g.menu.push_back(item(i18n::tr("Cut"), "scissors", Act::Cut));
        g.menu.push_back(item(i18n::tr("Copy"), "copy", Act::Copy));
        g.menu.push_back(item(i18n::tr("Copy Full Path"), "link", Act::CopyPath));
        g.menu.push_back(item(i18n::tr("Rename"), "pen", Act::Rename, 0, one));
        g.menu.push_back(item(i18n::tr("Move to Trash"), "trash-bin-minimalistic", Act::Trash));
        MenuItem more = item(i18n::tr("More Actions"), "list", Act::None);
        more.children.push_back(item(i18n::tr("Copy Name"), "clipboard-text", Act::CopyNames));
        more.children.push_back(item(i18n::tr("Duplicate"), "documents", Act::Duplicate));
        if (g.split) {
            const Pane& other = g.panes[1 - g.activePane];
            const bool enabled = other.cwd != pane().cwd && other.cwd != trashFiles();
            more.children.push_back(item(i18n::tr("Copy to Other Pane"), "copy", Act::CopyToPane, 0, enabled));
            more.children.push_back(item(i18n::tr("Move to Other Pane"), "folder-open", Act::MoveToPane, 0, enabled));
        }
        if (one && sel[0]->dir) {
            addFolderActions(more.children, true);
            if (!placeExists(sel[0]->path))
                more.children.push_back(item(i18n::tr("Add to Places"), "bookmark", Act::AddPlace));
        }
        g.menu.push_back(std::move(more));
        const size_t beforeTools = g.menu.size();
        // Conversions: only when every selected file is of the same family.
        bool allImage = true, allVideo = true, allAudio = true, allArchive = true;
        for (const Entry* e : sel) {
            allImage &= e->kind == Kind::Image && e->regular && extensionOf(e->name) != "svg";
            allVideo &= e->kind == Kind::Video && e->regular;
            allAudio &= e->kind == Kind::Audio && e->regular;
            allArchive &= e->kind == Kind::Archive && e->regular;
        }
        auto convertMenu = [&](const std::string& title, const char* ic, Group first, Group second, bool both) {
            MenuItem m = item(title, ic, Act::None);
            const auto& all = targets();
            const std::string ext0 = one ? extensionOf(sel[0]->name) : "";
            for (size_t i = 0; i < all.size(); i++) {
                if (all[i].group != first && !(both && all[i].group == second)) continue;
                if (ext0 == all[i].ext || (ext0 == "jpeg" && std::string(all[i].ext) == "jpg")) continue;  // not into what it already is
                if (both && all[i].group == second && !m.children.empty() && m.children.back().act == Act::Convert && all[m.children.back().arg].group == first) m.children.push_back(separatorItem());
                m.children.push_back(item(all[i].label, "", Act::Convert, static_cast<int>(i)));
            }
            return m;
        };
        if (allImage) g.menu.push_back(convertMenu(i18n::tr("Convert to"), "gallery-edit", Group::Image, Group::Image, false));
        if (allVideo) {
            g.menu.push_back(convertMenu(i18n::tr("Convert video to"), "video-frame-replace", Group::Video, Group::Video, false));
            g.menu.push_back(convertMenu(i18n::tr("Extract audio as"), "music-note", Group::Audio, Group::Audio, false));
        }
        if (allAudio) g.menu.push_back(convertMenu(i18n::tr("Convert to"), "music-note-slider", Group::Audio, Group::Audio, false));
        if (allArchive) {
            g.menu.push_back(item(i18n::tr("Extract"), "archive-down", Act::Extract));
            g.menu.push_back(item(i18n::tr("Extract here"), "archive-down-minimlistic", Act::ExtractHere));
        }
        MenuItem pack = item(i18n::tr("Compress to"), "archive-up", Act::None);
        const auto& formats = archiveFormats();
        for (size_t i = 0; i < formats.size(); i++) if (haveTool(formats[i].tool)) pack.children.push_back(item(formats[i].label, "", Act::Compress, static_cast<int>(i)));
        if (!pack.children.empty()) g.menu.push_back(std::move(pack));
        if (g.menu.size() > beforeTools)
            g.menu.insert(g.menu.begin() + beforeTools, separatorItem());
        g.menu.push_back(separatorItem());
        g.menu.push_back(item(i18n::tr("Select all"), "list-check", Act::SelectAll));
        return;
    }
    g.menu.push_back(item(i18n::tr("New folder"), "add-folder", Act::NewFolder));
    g.menu.push_back(item(i18n::tr("New file"), "document-add", Act::NewFile));
    g.menu.push_back(separatorItem());
    g.menu.push_back(item(i18n::tr("Paste"), "clipboard-add", Act::Paste, 0, !g.clip.empty()));
    g.menu.push_back(item(i18n::tr("Copy Folder Path"), "link", Act::CopyFolderPath));
    MenuItem folder = item(i18n::tr("Folder Actions"), "folder-open", Act::None);
    addFolderActions(folder.children, false);
    if (!placeExists(pane().cwd))
        folder.children.push_back(item(i18n::tr("Add this folder to Places"), "bookmark", Act::AddPlace, 1));
    g.menu.push_back(std::move(folder));
    g.menu.push_back(item(i18n::tr("Select all"), "list-check", Act::SelectAll));
    g.menu.push_back(separatorItem());
    g.menu.push_back(item(i18n::tr(g.showHidden ? "Hide hidden files" : "Show hidden files"), "eye", Act::Hidden));
    g.menu.push_back(item(i18n::tr("Refresh"), "restart", Act::Refresh));
}
// The menu of a row of the sidebar (a place of the user, the Trash, a drive...), or of the blank part of it (side == -1).
void buildSideMenu(int side) {
    g.menu.clear();
    g.menuSub = -1; g.menuOffset = 0;
    g.subPending = -2;
    if (side < 0 || side >= static_cast<int>(g_sideRows.size())) {
        g.menu.push_back(item(i18n::tr("Add this folder to Places"), "add-circle", Act::AddPlace, 1, !inTrash() && !placeExists(pane().cwd)));
        g.menu.push_back(item(i18n::tr("Reset Places"), "restart", Act::ResetPlaces));
        return;
    }
    g.menu.push_back(item(i18n::tr("Open"), "folder-open", Act::OpenPlace, side));
    g.menu.push_back(item(i18n::tr("Copy Full Path"), "link", Act::CopyPlacePath, side));
    if (side < static_cast<int>(g.places.size())) g.menu.push_back(item(i18n::tr("Remove from Places"), "close-circle", Act::RemovePlace, side));
    else if (g_sideRows[static_cast<size_t>(side)].path == trashFiles()) g.menu.push_back(item(i18n::tr("Empty the Trash"), "trash-bin-trash", Act::EmptyTrash));
}
void openMenu(bool onItem, int side = -2, bool address = false) {
    if (address) {
        g.menu = {item(i18n::tr("Edit Location"), "pen", Act::EditLocation),
                  item(i18n::tr("Copy Folder Path"), "link", Act::CopyFolderPath)};
        g.menuSub = -1; g.menuOffset = 0;
        g.subPending = -2;
        if (!inTrash()) addFolderActions(g.menu, false);
    } else if (side != -2) buildSideMenu(side);
    else buildMenu(onItem);
    g.menuOpen = true;
    g.menuX = g.px;
    g.menuY = g.py;
    g.dirty = true;
}
void closeMenu() { if (g.menuOpen) { g.menuOpen = false; g.menuSub = -1; g.menuOffset = 0; g.subPending = -2; g.dirty = true; } }
void runAct(const MenuItem& m) {
    switch (m.act) {
        case Act::AddPlace: { const auto sel = selectedEntries(); addPlace(m.arg == 1 || sel.empty() ? pane().cwd : sel[0]->path); break; }
        case Act::RemovePlace: removePlace(m.arg); break;
        case Act::ResetPlaces: resetPlaces(); g.places = loadPlaces(); g.dirty = true; break;
        case Act::OpenPlace: if (m.arg >= 0 && m.arg < static_cast<int>(g_sideRows.size())) navigate(g_sideRows[static_cast<size_t>(m.arg)].path); break;
        case Act::Open: { const auto sel = selectedEntries(); if (sel.size() == 1) for (size_t i = 0; i < pane().view.size(); i++) if (entryAt(static_cast<int>(i)).path == sel[0]->path) { openEntry(static_cast<int>(i)); break; } break; }
        case Act::OpenWith:
            if (m.application && !m.target.empty()) launchApplication(m.application.get(), m.target);
            break;
        case Act::RunExecutable: {
            std::error_code ec;
            if (fs::is_regular_file(m.target, ec) && access(m.target.c_str(), X_OK) == 0) {
                const std::string folder = fs::path(m.target).parent_path().string();
                launchDetached(m.target.c_str(), nullptr, folder.c_str(), true);
            } else say(i18n::tr("This file is no longer executable"));
            break;
        }
        case Act::SetView: setView(static_cast<ViewMode>(m.arg)); break;
        case Act::SetSort: if (pane().sort != static_cast<Sort>(m.arg)) setSort(static_cast<Sort>(m.arg)); break;
        case Act::SortOrder: pane().descending = m.arg != 0; resort(); break;
        case Act::FoldersFirst: pane().foldersFirst = !pane().foldersFirst; resort(); break;
        case Act::HiddenLast: pane().hiddenLast = !pane().hiddenLast; resort(); break;
        case Act::CopyToPane: case Act::MoveToPane: {
            if (!g.split) break;
            const std::string& to = g.panes[1 - g.activePane].cwd;
            auto paths = selectedPaths();
            if (!paths.empty() && to != pane().cwd && to != trashFiles())
                g.worker.submit({m.act == Act::CopyToPane ? Op::Copy : Op::Move, std::move(paths), to, {}});
            break;
        }
        case Act::Cut: copySelection(true); break;
        case Act::Copy: copySelection(false); break;
        case Act::CopyPath: copyText(selectedPaths()); break;
        case Act::EditLocation: openAddress(); break;
        case Act::CopyFolderPath: copyText({pane().cwd}); break;
        case Act::CopyPlacePath:
            if (m.arg >= 0 && m.arg < static_cast<int>(g_sideRows.size()))
                copyText({g_sideRows[static_cast<size_t>(m.arg)].path});
            break;
        case Act::CopyNames: {
            std::vector<std::string> names;
            for (const Entry* e : selectedEntries()) names.push_back(e->name);
            copyText(names);
            break;
        }
        case Act::Duplicate: {
            auto paths = selectedPaths();
            if (!paths.empty()) g.worker.submit({Op::Duplicate, std::move(paths), pane().cwd, {}});
            break;
        }
        case Act::NewWindow: {
            const std::string path = actionFolder(m.arg != 0);
            if (!path.empty()) launchDetached("/proc/self/exe", path.c_str());
            break;
        }
        case Act::Terminal: {
            const std::string path = actionFolder(m.arg != 0);
            if (!path.empty()) launchDetached(g.terminal.c_str(), nullptr, path.c_str());
            break;
        }
        case Act::Paste: pasteHere(); break;
        case Act::Rename: startRename(); break;
        case Act::Trash: trashSelection(); break;
        case Act::Delete: askDeleteForever(); break;
        case Act::Restore: restoreSelection(); break;
        case Act::EmptyTrash: if (!pane().entries.empty()) { pane().selected.clear(); selectAll(); g.dialog = Dlg::ConfirmDelete; } break;
        case Act::NewFolder: openDialog(Dlg::NewFolder, i18n::tr("New folder")); break;
        case Act::NewFile: openDialog(Dlg::NewFile, i18n::tr("New file")); break;
        case Act::Convert: convertSelection(m.arg); break;
        case Act::Compress: compressSelection(m.arg); break;
        case Act::Extract: extractSelection(true); break;
        case Act::ExtractHere: extractSelection(false); break;
        case Act::SelectAll: selectAll(); break;
        case Act::Refresh: reload(true); break;
        case Act::Hidden: toggleHidden(); break;
        default: break;
    }
}

// ---- the keys --------------------------------------------------------------------------------------------------------------------------
size_t prevChar(const std::string& s, size_t i) {
    if (i == 0) return 0;
    do i--; while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}
size_t nextChar(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    do i++; while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}
bool modActive(const char* name) { return g.xkbState && xkb_state_mod_name_is_active(g.xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0; }

bool addressKey(uint32_t key, xkb_keysym_t sym, bool ctrl, bool shift) {
    if (sym == XKB_KEY_Escape) { g.focus = 0; g.dirty = true; return false; }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) { submitAddress(); return false; }
    auto& p = pane();
    textsel::Field field(p.address.s, p.address.cur, p.addressAnchor);
    textsel::Key action = textsel::Key::None;
    switch (xkb_keysym_to_lower(sym)) {
        case XKB_KEY_Left: case XKB_KEY_KP_Left: action = textsel::Key::Left; break;
        case XKB_KEY_Right: case XKB_KEY_KP_Right: action = textsel::Key::Right; break;
        case XKB_KEY_Home: case XKB_KEY_KP_Home: action = textsel::Key::Home; break;
        case XKB_KEY_End: case XKB_KEY_KP_End: action = textsel::Key::End; break;
        case XKB_KEY_BackSpace: action = textsel::Key::Backspace; break;
        case XKB_KEY_Delete: case XKB_KEY_KP_Delete: action = textsel::Key::Delete; break;
        case XKB_KEY_a: if (ctrl) action = textsel::Key::SelectAll; break;
        case XKB_KEY_c: if (ctrl) action = textsel::Key::Copy; break;
        case XKB_KEY_x: if (ctrl) action = textsel::Key::Cut; break;
        case XKB_KEY_v: if (ctrl) action = textsel::Key::Paste; break;
    }
    g.caretOn = true;
    g.caretReset = Clock::now();
    g.dirty = true;
    if (action != textsel::Key::None) {
        const auto result = textsel::handleKey(field, action, ctrl, shift);
        if (result.wantCopy) copyText({result.copyText});
        if (result.wantPaste) {
            std::string clip;
            if (clipboard::paste(clip) && clip.size() < PATH_MAX &&
                g_utf8_validate(clip.data(), clip.size(), nullptr) && clip.find_first_of("\r\n") == std::string::npos)
                field.insert(clip);
        }
        return result.repeat;
    }
    if (ctrl) return false;
    char buf[16];
    const int count = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (count > 0 && count < static_cast<int>(sizeof buf) && static_cast<unsigned char>(buf[0]) >= 32 && buf[0] != 127) {
        field.insert(std::string(buf, count));
        return true;
    }
    return false;
}

// A key while a text field has the keyboard. True when it can repeat.
bool fieldKey(uint32_t key, xkb_keysym_t sym, bool ctrl) {
    Field& f = g.focus == 1 ? pane().search : g.dialogField;
    f.cur = std::min(f.cur, f.s.size());
    g.caretReset = Clock::now();
    g.dirty = true;
    const bool isSearch = g.focus == 1;
    switch (sym) {
        case XKB_KEY_Escape:
            if (g.dialog != Dlg::None) closeDialog();
            else if (isSearch && !pane().search.s.empty()) { pane().search = {}; pane().viewDirty = true; }
            else g.focus = 0;
            return false;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter:
            if (g.dialog != Dlg::None) commitDialog();
            else { g.focus = 0; if (pane().view.size() == 1) openEntry(0); }
            return false;
        case XKB_KEY_Left: f.cur = prevChar(f.s, f.cur); return true;
        case XKB_KEY_Right: f.cur = nextChar(f.s, f.cur); return true;
        case XKB_KEY_Home: f.cur = 0; return false;
        case XKB_KEY_End: f.cur = f.s.size(); return false;
        case XKB_KEY_BackSpace:
            if (f.cur > 0) { const size_t p = prevChar(f.s, f.cur); f.s.erase(p, f.cur - p); f.cur = p; if (isSearch) pane().viewDirty = true; }
            return true;
        case XKB_KEY_Delete:
            if (f.cur < f.s.size()) { f.s.erase(f.cur, nextChar(f.s, f.cur) - f.cur); if (isSearch) pane().viewDirty = true; }
            return true;
        case XKB_KEY_Down: if (isSearch && !pane().view.empty()) { g.focus = 0; selectOnly(0); scrollToCursor(); } return false;
        default: break;
    }
    if (ctrl) return false;
    char buf[16];
    const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f || f.s.size() > 240) return false;
    f.s.insert(f.cur, buf, static_cast<size_t>(n));
    f.cur += static_cast<size_t>(n);
    if (isSearch) pane().viewDirty = true;
    return true;
}

void moveCursor(int delta, bool extend) {
    if (pane().view.empty()) return;
    const int count = static_cast<int>(pane().view.size());
    int to = pane().cursor < 0 ? (delta > 0 ? 0 : count - 1) : std::clamp(pane().cursor + delta, 0, count - 1);
    if (extend) selectRange(to); else selectOnly(to);
    scrollToCursor();
}

int arrowStep(xkb_keysym_t key) {
    const bool lateral = key == XKB_KEY_Left || key == XKB_KEY_Right;
    const int distance = horizontal() ? (lateral ? pane().rows : 1) : (lateral ? 1 : pane().columns);
    return key == XKB_KEY_Left || key == XKB_KEY_Up ? -distance : distance;
}

int pageStep() {
    if (horizontal())
        return std::max(1, static_cast<int>(pane().content.w / std::max(1.0, pane().cellW))) * pane().rows;
    return std::max(1, static_cast<int>(pane().content.h / std::max(1.0, pane().cellH))) * pane().columns;
}

bool handleKey(uint32_t key) {
    if (!g.xkbState) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, key + 8);
    const bool ctrl = modActive(XKB_MOD_NAME_CTRL), shift = modActive(XKB_MOD_NAME_SHIFT), alt = modActive(XKB_MOD_NAME_ALT);
    if (g.pressedPlace != -1 && sym == XKB_KEY_Escape) { cancelPlacePress(); return false; }
    if (g.dialog == Dlg::ConfirmDelete) {
        if (sym == XKB_KEY_Escape) closeDialog();
        else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) commitDialog();
        return false;
    }
    if (g.menuOpen) { if (sym == XKB_KEY_Escape) closeMenu(); return false; }
    if (ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_w)) { g.running = false; return false; }
    if (sym == XKB_KEY_F3) {
        if (ctrl && g.split) focusPane(1 - g.activePane);
        else if (!ctrl) toggleSplit();
        return false;
    }
    if (g.dialog == Dlg::None && (sym == XKB_KEY_F6 || (ctrl && xkb_keysym_to_lower(sym) == XKB_KEY_l) || (alt && xkb_keysym_to_lower(sym) == XKB_KEY_d))) {
        if (sym == XKB_KEY_F6 && g.focus == 3) { g.focus = 0; g.dirty = true; }
        else openAddress();
        return false;
    }
    if (g.focus == 3) return addressKey(key, sym, ctrl, shift);
    if (g.focus != 0) return fieldKey(key, sym, ctrl);
    if (ctrl) {
        switch (sym) {
            case XKB_KEY_a: case XKB_KEY_A: selectAll(); return false;
            case XKB_KEY_c: case XKB_KEY_C: copySelection(false); return false;
            case XKB_KEY_x: case XKB_KEY_X: copySelection(true); return false;
            case XKB_KEY_v: case XKB_KEY_V: pasteHere(); return false;
            case XKB_KEY_h: case XKB_KEY_H: toggleHidden(); return false;
            case XKB_KEY_f: case XKB_KEY_F: g.focus = 1; pane().search.cur = pane().search.s.size(); g.caretReset = Clock::now(); g.dirty = true; return false;
            case XKB_KEY_r: case XKB_KEY_R: reload(true); return false;
            case XKB_KEY_1: setView(ViewMode::Icons); return false;
            case XKB_KEY_2: setView(ViewMode::Details); return false;
            case XKB_KEY_3: setView(ViewMode::Compact); return false;
            case XKB_KEY_Left: case XKB_KEY_Right: case XKB_KEY_Up: case XKB_KEY_Down: {  // Ctrl+arrows: the focus moves, the selection stays
                const int step = arrowStep(sym);
                if (!pane().view.empty()) { pane().cursor = std::clamp((pane().cursor < 0 ? 0 : pane().cursor) + step, 0, static_cast<int>(pane().view.size()) - 1); scrollToCursor(); g.dirty = true; }
                return true;
            }
            case XKB_KEY_space: if (pane().cursor >= 0 && pane().cursor < static_cast<int>(pane().view.size())) toggleOne(pane().cursor); return false;  // Ctrl+Space: toggles the one in focus
            case XKB_KEY_N: if (shift) openDialog(Dlg::NewFolder, i18n::tr("New folder")); return false;
            case XKB_KEY_n: if (shift) openDialog(Dlg::NewFolder, i18n::tr("New folder")); return false;
            default: return false;
        }
    }
    if (alt) {
        if (sym == XKB_KEY_Left) goBack();
        else if (sym == XKB_KEY_Right) goForward();
        else if (sym == XKB_KEY_Up) goUp();
        return false;
    }
    switch (sym) {
        case XKB_KEY_Left: case XKB_KEY_Right: case XKB_KEY_Up: case XKB_KEY_Down:
            moveCursor(arrowStep(sym), shift);
            return true;
        case XKB_KEY_Prior: moveCursor(-pageStep(), shift); return false;
        case XKB_KEY_Next: moveCursor(pageStep(), shift); return false;
        case XKB_KEY_Home: if (!pane().view.empty()) { if (shift) selectRange(0); else selectOnly(0); scrollToCursor(); } return false;
        case XKB_KEY_End: if (!pane().view.empty()) { const int last = static_cast<int>(pane().view.size()) - 1; if (shift) selectRange(last); else selectOnly(last); scrollToCursor(); } return false;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter: if (pane().cursor >= 0 && pane().cursor < static_cast<int>(pane().view.size())) openEntry(pane().cursor); return false;
        case XKB_KEY_BackSpace: goUp(); return false;
        case XKB_KEY_Delete: if (shift) askDeleteForever(); else trashSelection(); return false;
        case XKB_KEY_space: if (pane().cursor >= 0 && pane().cursor < static_cast<int>(pane().view.size())) selectOnly(pane().cursor); return false;
        case XKB_KEY_F2: startRename(); return false;
        case XKB_KEY_F5: reload(true); return false;
        case XKB_KEY_Escape: if (!pane().selected.empty()) { pane().selected.clear(); pane().selectionDirty = true; g.dirty = true; } else if (!pane().search.s.empty()) { pane().search = {}; pane().viewDirty = true; } return false;
        default: break;
    }
    char buf[16];  // typing a letter starts the search (no need to click the field first)
    const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n > 0 && static_cast<unsigned char>(buf[0]) > 0x20 && buf[0] != 0x7f) {
        g.focus = 1;
        pane().search.cur = pane().search.s.size();
        return fieldKey(key, sym, false);
    }
    return false;
}

// ---- the mouse -------------------------------------------------------------------------------------------------------------------------
void updateCursor();
void setHoverImpl() {
    int id = None, arg = 0, hitPane = -1;
    if (g.inside) for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it) if (it->r.contains(g.px, g.py)) { id = it->id; arg = it->arg; hitPane = it->pane; break; }
    if (id != g.hoverId || arg != g.hoverArg || hitPane != g.hoverPane) {
        g.hoverId = id;
        g.hoverArg = arg;
        g.hoverPane = hitPane;
        g.dirty = true;
        if (g.menuOpen && id != MenuRow) g.subPending = -2;  // off the menu: whatever was waiting is dropped
        if (g.menuOpen && id == MenuRow) {
            if (arg >= 1000 || arg < 0) g.subPending = -2;  // in the second panel (or on the background of a panel): the open one stays
            else {
                const int sub = arg < static_cast<int>(g.menu.size()) && !g.menu[static_cast<size_t>(arg)].children.empty() ? arg : -1;
                if (sub == g.menuSub) g.subPending = -2;
                else if (g.menuSub < 0) { g.menuSub = sub; g.menuOffset = 0; }  // nothing open: the panel follows the pointer at once
                else { g.subPending = sub; g.subPendingAt = Clock::now(); }  // one is open: the pointer may be on its way to it across other rows, so wait a moment
            }
        }
    }
}
void setHover() { setHoverImpl(); updateCursor(); }

// The lasso follows the pointer: whatever its rectangle touches is selected (with Ctrl held at the start, on top of what was already selected).
void updateLasso() {
    pane().lassoX1 = std::clamp(g.px - pane().content.x, 0.0, pane().content.w) + (horizontal() ? pane().scroll : 0);
    pane().lassoY1 = std::clamp(g.py - pane().content.y, 0.0, pane().content.h) + (horizontal() ? 0 : pane().scroll);
    const double x0 = std::min(pane().lassoX0, pane().lassoX1), x1 = std::max(pane().lassoX0, pane().lassoX1), y0 = std::min(pane().lassoY0, pane().lassoY1), y1 = std::max(pane().lassoY0, pane().lassoY1);
    pane().selected = pane().lassoBase;
    pane().selectionDirty = true;
    for (int i = 0; i < static_cast<int>(pane().view.size()); i++) {
        const Rect r = itemRect(i);
        if (horizontal() ? r.x > x1 : r.y > y1) break;
        if (r.x < x1 && r.x + r.w > x0 && r.y < y1 && r.y + r.h > y0) pane().selected.insert(entryAt(i).path);
    }
    g.dirty = true;
}
// The pointer: an arrow by default, a hand over what can be clicked, a text cursor over the fields. (Without this the compositor may show whatever it had.)
void updateCursor() {
    if (!g.cursorMgr || !g.pointer || !g.inside) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    int shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (g.placeDrag) {  // picked up: a closed hand
        if (g.cursorShape != WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING) { g.cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING; wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, g.cursorShape); }
        return;
    }
    switch (g.hoverId) {
        case SearchField: case DialogField: case AddressField: case AddressBar: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT; break;
        case None: case Empty: case Scrim: case SideEmpty: break;
        case ScrollThumb: case ScrollTrack: break;  // (an arrow over the scroll bar, as everywhere)
        case MenuRow: if (g.hoverArg >= 0) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER; break;
        default: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER; break;
    }
    if (g.dialog != Dlg::None && g.hoverId == Scrim) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (shape == g.cursorShape) return;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}

// Dragging the scroll bar: the handle follows the pointer, and the folder with it.
double pointerOnScrollAxis() { return horizontal() ? g.px : g.py; }

void dragScrollTo() {
    const Rect& track = pane().scrollTrack;
    const double start = horizontal() ? track.x : track.y;
    const double length = horizontal() ? track.w : track.h;
    const double travel = length - pane().thumbH;
    if (travel <= 0 || pane().scrollMax <= 0) return;
    const double top = std::clamp(pointerOnScrollAxis() - pane().dragScrollOffset, start, start + travel);
    pane().scroll = (top - start) / travel * pane().scrollMax;
    g.dirty = true;
}
void onMotion() {
    if (g.pressedPlace >= 0) {  // a button is down on a place: moving before the hold counts cancels it; once picked up, the pointer sets where it lands
        if (g.placeDrag) { updateDrop(); g.dirty = true; }
        else if (std::abs(g.py - g.pressY) > 8 || std::abs(g.px - g.pressX) > 8) g.pressedPlace = -2;
    }
    if (pane().dragScroll) { dragScrollTo(); return; }
    if (pane().lasso) { updateLasso(); return; }
    setHover();
}
void onRelease() {
    if (g.pressedPlace != -1) {
        const int row = g.pressedPlace;
        const bool dragging = g.placeDrag, stillOnIt = g.hoverId == PlaceRow && g.hoverArg == row;
        const int to = g.dropIndex;
        cancelPlacePress();
        if (dragging) movePlace(row, to);
        else if (row >= 0 && stillOnIt && row < static_cast<int>(g_sideRows.size())) navigate(g_sideRows[static_cast<size_t>(row)].path);
        return;
    }
    if (pane().dragScroll) { pane().dragScroll = false; g.dirty = true; } if (pane().lasso) { pane().lasso = false; g.dirty = true; } }

void activateMenuRow(int arg) {
    const MenuItem* m = nullptr;
    if (arg >= 1000) {
        if (g.menuSub >= 0 && g.menuSub < static_cast<int>(g.menu.size())) {
            const auto& kids = g.menu[static_cast<size_t>(g.menuSub)].children;
            if (arg - 1000 < static_cast<int>(kids.size())) m = &kids[static_cast<size_t>(arg - 1000)];
        }
    } else if (arg >= 0 && arg < static_cast<int>(g.menu.size())) m = &g.menu[static_cast<size_t>(arg)];
    if (!m || !m->enabled) return;
    if (!m->children.empty()) { if (g.menuSub != arg) g.menuOffset = 0; g.menuSub = arg; g.dirty = true; return; }  // (a click on a row with a second panel opens it)
    const MenuItem copy = *m;
    if (copy.act == Act::OtherApplications) { showOtherApplications(copy.target); return; }
    closeMenu();
    runAct(copy);
}

void onClick(uint32_t button) {
    setHover();
    const bool right = button == 0x111;
    if (g.dialog != Dlg::None) {
        if (g.hoverId == DialogOk) commitDialog();
        else if (g.hoverId == DialogCancel || (g.hoverId == Scrim && g.hoverArg == 0)) closeDialog();
        else if (g.hoverId == DialogField) { g.focus = 2; g.dialogField.cur = g.dialogField.s.size(); g.dirty = true; }
        return;
    }
    if (g.menuOpen) {
        if (g.hoverId == MenuRow && !right) { activateMenuRow(g.hoverArg); return; }
        if (g.hoverId == MenuRow) return;
        closeMenu();  // a click anywhere else only closes it (a right click goes on to open a new one)
        if (!right) return;
    }
    focusPane(g.hoverPane);
    if (g.hoverId == Item && (g.hoverArg < 0 || g.hoverArg >= static_cast<int>(pane().view.size()))) return;
    if (g.hoverId != SearchField && g.hoverId != SearchClear && g.hoverId != AddressField && g.hoverId != BtnGo && g.focus != 0) { g.focus = 0; g.dirty = true; }
    if (button == 0x113) { goBack(); return; }     // the "back" button of the mouse
    if (button == 0x114) { goForward(); return; }  // and the "forward" one
    if (right) {
        if (g.hoverId == Item) { if (!pane().selected.count(entryAt(g.hoverArg).path)) selectOnly(g.hoverArg); openMenu(true); }
        else if (g.hoverId == AddressBar || g.hoverId == AddressField || g.hoverId == Crumb || g.hoverId == BtnCopyPath) openMenu(false, -2, true);
        else if (g.hoverId == PlaceRow) openMenu(false, g.hoverArg);
        else if (g.hoverId == SideEmpty) openMenu(false, -1);
        else if (g.hoverId == Empty || g.hoverId == None) { pane().selected.clear(); pane().selectionDirty = true; openMenu(false); }
        return;
    }
    if (button != 0x110) return;
    switch (g.hoverId) {
        case BtnBack: goBack(); break;
        case BtnForward: goForward(); break;
        case BtnUp: goUp(); break;
        case BtnClose: g.running = false; break;
        case BtnView: openViewMenu(false); break;
        case BtnSplit: toggleSplit(); break;
        case BtnHidden: toggleHidden(); break;
        case BtnCopyPath: copyText({pane().cwd}); break;
        case AddressBar: openAddress(false); break;
        case AddressField: {
            Pane& p = pane();
            textsel::Field field(p.address.s, p.address.cur, p.addressAnchor);
            PangoLayout* layout = addressLayout(g_measure, p.address.s);
            field.setCaret(textsel::indexAtX(layout, p.address.s, g.px - p.addressTextX + p.addressScroll),
                         modActive(XKB_MOD_NAME_SHIFT));
            g_object_unref(layout);
            g.caretOn = true;
            g.caretReset = Clock::now();
            g.dirty = true;
            break;
        }
        case BtnGo: submitAddress(); break;
        case BtnSort:
            if (g.hoverArg >= 10) setSort(static_cast<Sort>(g.hoverArg - 10));
            else openViewMenu(true);
            break;
        case SearchField: g.focus = 1; pane().search.cur = pane().search.s.size(); g.dirty = true; break;
        case SearchClear: pane().search = {}; pane().viewDirty = true; g.dirty = true; break;
        case Crumb: {
            const auto& paths = pane().crumbPaths;
            if (g.hoverArg >= 0 && g.hoverArg < static_cast<int>(paths.size())) {
                const auto& path = paths[static_cast<size_t>(g.hoverArg)];
                if (path == pane().cwd) openAddress(false);
                else navigate(path);
            }
            break;
        }
        case PlaceRow:  // it opens when the button is let go; held for a moment, it is picked up instead (see tick)
            g.pressedPlace = g.hoverArg;
            g.pressAt = Clock::now();
            g.pressX = g.px;
            g.pressY = g.py;
            g.placeDrag = false;
            break;
        case ScrollThumb: pane().dragScroll = true; pane().dragScrollOffset = pointerOnScrollAxis() - pane().thumbTop; g.dirty = true; break;
        case ScrollTrack:  // a click on the track: the handle jumps to the pointer and can be dragged from there
            pane().dragScroll = true;
            pane().dragScrollOffset = pane().thumbH / 2;
            dragScrollTo();
            break;
        case StatusCancel: g.worker.cancelAll(); break;
        case DiskSpace: g.spaceAsPercent = !g.spaceAsPercent; g.dirty = true; break;
        case Empty:
            pane().lassoBase = modActive(XKB_MOD_NAME_CTRL) || modActive(XKB_MOD_NAME_SHIFT) ? pane().selected : std::set<std::string>{};
            pane().selected = pane().lassoBase;
            pane().selectionDirty = true;
            pane().lasso = true;
            pane().lassoX0 = std::clamp(g.px - pane().content.x, 0.0, pane().content.w) + (horizontal() ? pane().scroll : 0);
            pane().lassoY0 = std::clamp(g.py - pane().content.y, 0.0, pane().content.h) + (horizontal() ? 0 : pane().scroll);
            pane().lassoX1 = pane().lassoX0;
            pane().lassoY1 = pane().lassoY0;
            g.dirty = true;
            break;
        case Item: {
            const int i = g.hoverArg;
            const auto now = Clock::now();
            const bool ctrl = modActive(XKB_MOD_NAME_CTRL), shift = modActive(XKB_MOD_NAME_SHIFT);
            if (ctrl && shift) { const auto base = pane().selected; selectRange(i); pane().selected.insert(base.begin(), base.end()); }
            else if (ctrl) toggleOne(i);
            else if (shift) selectRange(i);
            else if (i == pane().lastClickIndex && now - pane().lastClick < std::chrono::milliseconds(400)) { openEntry(i); pane().lastClickIndex = -1; return; }
            else selectOnly(i);
            pane().lastClickIndex = i;
            pane().lastClick = now;
            break;
        }
        default: break;
    }
}
void onWheel(double v, bool horizontalAxis = false) {
    if (g.menuOpen) {
        if (!horizontalAxis && g.menuSub >= 0 && g.subRect.contains(g.px, g.py)) {
            const auto& children = g.menu[static_cast<size_t>(g.menuSub)].children;
            const int limit = menuOffsetLimit(children, g.height);
            g.menuOffset = std::clamp(g.menuOffset + (v > 0 ? 1 : -1), 0, limit);
            g.dirty = true;
        }
        return;
    }
    if (g.dialog != Dlg::None) return;
    if (g.split && g.hoverPane >= 0) g.hitPane = g.hoverPane;
    if (pane().card.contains(g.px, g.py) && (!horizontalAxis || horizontal())) {
        elasticWheel(pane().band, pane().scroll, pane().scrollMax, v * 3);
        g.dirty = true;
    }
    g.hitPane = -1;
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
    if (!hidpi::begin(frame, width, height, scale, "end4pC-fil3s")) return nullptr;
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
    setHover();  // the hit areas changed with the drawing
}

void onWmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmListener = {onWmPing};
void onXdgConfigure(void*, xdg_surface* s, uint32_t serial) { xdg_surface_ack_configure(s, serial); g.configured = true; g.dirty = true; }
const xdg_surface_listener kXdgSurfaceListener = {onXdgConfigure};
void onTopConfigure(void*, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
    if (w > 0 && h > 0) { g.width = std::max(kMinW, w); g.height = std::max(kMinH, h); }
    g.dirty = true;
}
void onTopClose(void*, xdg_toplevel*) { g.running = false; }
const xdg_toplevel_listener kToplevelListener = {onTopConfigure, onTopClose, nullptr, nullptr};

void onPEnter(void*, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) { g.enterSerial = serial; g.cursorShape = -1; g.inside = true; g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); setHover(); }
void onPLeave(void*, wl_pointer*, uint32_t, wl_surface*) { g.inside = false; setHover(); }
void onPMotion(void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) { g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); onMotion(); }
void onPButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) { if (state == WL_POINTER_BUTTON_STATE_PRESSED) onClick(button); else if (button == 0x110) onRelease(); }
void onPAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) { onWheel(wl_fixed_to_double(value), axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL); }
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
    if (!g.display) { std::fprintf(stderr, "end4pC-fil3s: no Wayland display\n"); return false; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    if (wl_display_roundtrip(g.display) < 0 || !g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-fil3s: missing Wayland globals\n"); return false; }
    wl_display_roundtrip(g.display);
    g.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(g.scale, g.surface, g.viewporter, g.fscaleMgr, [] { g.dirty = true; });
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, "Fil3s");
    xdg_toplevel_set_app_id(g.toplevel, "end4pC-fil3s");
    xdg_toplevel_set_min_size(g.toplevel, kMinW, kMinH);
    wl_surface_commit(g.surface);
    return true;
}

void updateStatus(Clock::time_point now) {
    Pane& p = pane();
    const auto result = p.statusProbe->take();
    if (result.serial != 0 && result.serial == p.statusSerial) {
        p.space = result.space;
        p.spaceAt = now;
        p.statusPending = false;
        if (result.measured) {
            p.selectionBytes = result.bytes;
            p.sizePartial = result.partial;
            p.sizePending = false;
        }
        g.dirty = true;
    }
    if (p.cwd.empty() || p.lasso) return;

    StatusRequest request;
    if (p.selectionDirty) {
        p.selectionDirty = false;
        p.selectionFolders = p.selectionFiles = 0;
        p.selectionName.clear();
        p.selectionType.clear();
        p.sizePartial = false;
        p.selectionBytes = 0;
        for (const auto* entry : selectedEntries()) {
            if (p.selectionFolders + p.selectionFiles == 0) {
                p.selectionName = entry->name;
                p.selectionType = entryType(*entry);
            }
            if (entry->dir) ++p.selectionFolders;
            else ++p.selectionFiles;
            if (entry->dir && !entry->link) request.folders.push_back(entry->path);
            else if (entry->regular || entry->link) p.selectionBytes += entry->size;
        }
        p.sizePending = !request.folders.empty();
        request.bytes = p.selectionBytes;
        request.measure = p.sizePending;
        g.dirty = true;
    } else if (p.statusPending || now - p.spaceAt < std::chrono::seconds(5)) return;
    request.serial = ++p.statusSerial;
    request.path = p.cwd;
    p.statusPending = true;
    p.statusProbe->request(std::move(request));
}

// Once per turn of the loop: what the threads finished, what changed on disk, the colours, the repeat of a key.
void tick() {
    const auto now = Clock::now();
    for (int index = 0; index < 2; ++index) {
        if (!g.split && index != g.activePane) continue;
        g.hitPane = index;
        if (pane().reloadAt != Clock::time_point{} && now >= pane().reloadAt) {
            pane().reloadAt = {};
            reload(true);
        }
        Listing l;
        if (pane().lister->take(l) && l.generation == pane().generation) applyListing(l);
        if (pane().band.step()) g.dirty = true;
        if (pane().viewDirty) rebuildView();
        updateStatus(now);
    }
    g.hitPane = -1;
    bool refresh[2]{};
    {
        std::lock_guard<std::mutex> lock(g.worker.mutex);
        if (g.worker.changed) {
            g.worker.changed = false;
            g.dirty = true;
            if (g.worker.textCopied) { say(i18n::tr("Copied to the clipboard")); g.worker.textCopied = false; }
            if (!g.worker.failure.empty()) { say(g.worker.failure); g.worker.failure.clear(); }
        }
        if (!g.worker.running && (g.status == "running" || !g.worker.outputs.empty())) {
            refresh[g.activePane] = true;
            for (const std::string& output : g.worker.outputs) {
                const auto parent = fs::path(output).parent_path();
                for (int index = 0; index < 2; ++index) {
                    if ((!g.split && index != g.activePane) || g.panes[index].cwd != parent) continue;
                    g.panes[index].selectAfter.insert(output);
                    refresh[index] = true;
                }
            }
            g.worker.outputs.clear();
        }
        g.status = g.worker.running ? "running" : "";
        if (g.worker.busy && g.worker.fraction < 0) g.dirty = true;
    }
    for (int index = 0; index < 2; ++index) if (refresh[index]) {
        g.hitPane = index;
        reload(true);
    }
    g.hitPane = -1;
    if (g.repeatKey && now >= g.repeatAt) {
        if (!handleKey(g.repeatKey)) g.repeatKey = 0;
        g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
    }
    if (mtimeOf(colorsPath()) != g_colorsStamp) { loadColors(); g.dirty = true; }
    if (pane().lasso) {  // near the top or bottom edge the folder scrolls by itself, so the lasso can reach what is out of sight
        const double edge = 36, speed = 14;
        const double start = horizontal() ? pane().content.x : pane().content.y;
        const double length = horizontal() ? pane().content.w : pane().content.h;
        const double pointer = pointerOnScrollAxis();
        const double above = start + edge - pointer, below = pointer - (start + length - edge);
        if (above > 0) pane().scroll = std::max(0.0, pane().scroll - speed * std::min(1.0, above / edge));
        else if (below > 0) pane().scroll = std::min(pane().scrollMax, pane().scroll + speed * std::min(1.0, below / edge));
        updateLasso();
    }
    if (g.menuOpen && g.subPending != -2 && now - g.subPendingAt > std::chrono::milliseconds(320)) {  // the pointer stayed on another row: its panel (or none) replaces the open one
        g.menuSub = g.subPending;
        g.menuOffset = 0;
        g.subPending = -2;
        g.dirty = true;
    }
    if (g.pressedPlace >= 0 && !g.placeDrag && g.pressedPlace < static_cast<int>(g.places.size()) && now - g.pressAt > std::chrono::milliseconds(450)) {
        g.placeDrag = true;  // held long enough: the place is picked up
        g.dropIndex = g.pressedPlace;
        g.dirty = true;
        updateCursor();
    }
    if (g.animating) g.dirty = true;  // a fade is running
    if (now - g.drivesAt > std::chrono::seconds(4)) {
        g.drivesAt = now;
        auto d = driveList();
        bool same = d.size() == g.drives.size();
        for (size_t i = 0; same && i < d.size(); i++) same = d[i].path == g.drives[i].path;
        if (!same) { g.drives = std::move(d); g.dirty = true; }
    }
    if (!g.toast.empty()) g.dirty = true;
    if (g.focus != 0) {
        const bool on = std::chrono::duration_cast<std::chrono::milliseconds>(now - g.caretReset).count() / 530 % 2 == 0;
        if (on != g.caretOn) { g.caretOn = on; g.dirty = true; }
    }
    g.lastTick = now;
}

int pollTimeoutBase() {
    if (pane().band.busy() || (g.split && g.panes[1 - g.activePane].band.busy()) ||
        pane().lasso || g.animating || g.placeDrag) return 16;
    for (const Pane& p : g.panes) if (p.reloadAt != Clock::time_point{}) return 33;
    if (g.pressedPlace >= 0) return 33;
    if (g.menuOpen && g.subPending != -2) return 33;
    if (g.worker.busy || !g.toast.empty()) return 33;
    if (g.focus != 0) return 120;
    return 500;
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
    g.worker.shutdown();
    g.thumbs.shutdown();
    for (const Pane& p : g.panes) if (p.inotifyFd >= 0) close(p.inotifyFd);
    if (g.frameCallback) wl_callback_destroy(g.frameCallback);
    if (g.toplevel) xdg_toplevel_destroy(g.toplevel);
    if (g.xsurface) xdg_surface_destroy(g.xsurface);
    hidpi::detach(g.scale);
    if (g.surface) wl_surface_destroy(g.surface);
    for (auto& b : g.buffers) b.clear();
    if (g.display) { wl_display_flush(g.display); wl_display_disconnect(g.display); }
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
    std::string terminal;
    if (jsonValue(cfg, "terminal", terminal) && !terminal.empty()) g.terminal = std::move(terminal);
    g.showHidden = readFile(hiddenPreferenceFile()) == "1";
    std::string mat, style, scale;
    matsym::assetsDir() = assetsDir();
    matsym::enabled() = jsonValue(cfg, "materialIcons", mat) && mat == "1";
    if (jsonValue(cfg, "iconScale", scale) && !scale.empty()) g_iconScale = std::clamp(std::atoi(scale.c_str()), 60, 150);
    if (jsonValue(cfg, "iconStyle", style) && !style.empty()) g_iconStyle = style;
    i18n::load(lang, assetsDir(), dataDir() + "/i18n");
    applyShellFont(fontFamilyFromConfig(dataDir() + "/config.json"), fontWeightFromConfig(dataDir() + "/config.json", "fontWeightText", 450), fontWeightFromConfig(dataDir() + "/config.json", "fontWeightTitle", 550));
    loadColors();
    if (!connectWayland()) return 1;
    cairo_surface_t* dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* dcr = mkcr(dummy);
    g_measure = pango_cairo_create_layout(dcr);

    int wake[2];
    if (pipe2(wake, O_CLOEXEC | O_NONBLOCK) < 0) return 1;
    g_wakeFd = wake[1];
    for (Pane& p : g.panes) {
        p.lister->wakeFd = wake[1];
        p.statusProbe->wakeFd = wake[1];
        p.inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    }
    g.thumbs.start(wake[1]);
    g.worker.start(wake[1]);
    g.places = loadPlaces();
    g.drives = driveList();

    std::string first = homeDir();
    if (argc > 1) {
        std::error_code ec;
        const fs::path given = fs::weakly_canonical(argv[1], ec);
        first = !ec && fs::is_directory(given, ec) ? given.string() : !ec && fs::exists(given, ec) ? given.parent_path().string() : first;
    }
    navigate(first, false);

    std::signal(SIGTERM, onTermSignal);
    std::signal(SIGINT, onTermSignal);
    std::signal(SIGHUP, onTermSignal);
    std::signal(SIGPIPE, SIG_IGN);
    while (g.running && !g_term) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);
        pollfd fds[] = {{wl_display_get_fd(g.display), POLLIN, 0}, {wake[0], POLLIN, 0},
                        {g.panes[0].inotifyFd, POLLIN, 0}, {g.panes[1].inotifyFd, POLLIN, 0}};
        if (poll(fds, 4, pollTimeout()) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & POLLIN) {
            char junk[64];
            while (read(wake[0], junk, sizeof junk) > 0) {}
            g.dirty = true;
        }
        for (int index = 0; index < 2; ++index) {
            if (!(fds[index + 2].revents & POLLIN)) continue;
            char junk[4096];
            while (read(g.panes[index].inotifyFd, junk, sizeof junk) > 0) {}
            if (g.split || g.activePane == index)
                g.panes[index].reloadAt = Clock::now() + std::chrono::milliseconds(200);
        }
        tick();
        if (g.dirty) render();
    }
    teardown();
    g_object_unref(g_measure);
    cairo_destroy(dcr);
    cairo_surface_destroy(dummy);
    return 0;
}
