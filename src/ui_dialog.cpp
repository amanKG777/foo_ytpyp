#include <foobar2000/SDK/foobar2000.h>
#include "ui_dialog.h"
#include "ytpipe_api.h"
#include <commctrl.h>
#include <vector>
#include <string>
#include <thread>

#pragma comment(lib, "comctl32.lib")

namespace {

// Upper bound on videos pulled from one playlist or channel.
constexpr int kMaxExpandItems = 5000;

struct YtItem {
    int kind = YTPIPE_KIND_VIDEO;
    std::string id;
    std::string title;
    std::string channel;
    std::string info;
    int duration_sec = 0;
};

void ItemCollector(int kind, const char* id, const char* title, const char* channel,
                   const char* info, int duration_sec, void* user_data) {
    auto* items = reinterpret_cast<std::vector<YtItem>*>(user_data);
    YtItem item;
    item.kind = kind;
    item.id = id ? id : "";
    item.title = title ? title : "";
    item.channel = channel ? channel : "";
    item.info = info ? info : "";
    item.duration_sec = duration_sec;
    items->push_back(std::move(item));
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring GetControlText(HWND h) {
    int len = GetWindowTextLengthW(h);
    std::wstring buf(len + 1, L'\0');
    GetWindowTextW(h, &buf[0], len + 1);
    buf.resize(len);
    return buf;
}

HINSTANCE MyInstance() {
    return core_api::get_my_instance();
}

std::string ItemUrl(const YtItem& item) {
    switch (item.kind) {
    case YTPIPE_KIND_PLAYLIST: return "https://www.youtube.com/playlist?list=" + item.id;
    case YTPIPE_KIND_CHANNEL: return "https://www.youtube.com/channel/" + item.id;
    default: return "https://www.youtube.com/watch?v=" + item.id;
    }
}

// Adds videos to a playlist. With a playlist_name, a new playlist of that name is created;
// otherwise the active playlist is used (or a "YouTube" playlist if the active one is locked).
// Titles/durations already known from search or playlist listings are stored as metadata
// hints, so foobar2000 doesn't have to resolve every track over the network just to show it.
// Returns the number of tracks added. Main thread only.
size_t AddVideos(const std::vector<YtItem>& videos, bool play_now, const char* playlist_name) {
    pfc::list_t<metadb_handle_ptr> items;
    auto db = metadb::get();
    auto hints = metadb_hint_list::create();
    bool any_hints = false;

    for (const auto& v : videos) {
        if (v.kind != YTPIPE_KIND_VIDEO || v.id.empty()) continue;
        pfc::string8 path = PFC_string_formatter() << "youtube://" << v.id.c_str();
        metadb_handle_ptr handle;
        db->handle_create(handle, make_playable_location(path.c_str(), 0));
        items.add_item(handle);

        metadb_info_container::ptr existing;
        if (!v.title.empty() && !handle->get_info_ref(existing)) {
            file_info_impl info;
            info.meta_set("title", v.title.c_str());
            if (!v.channel.empty()) info.meta_set("artist", v.channel.c_str());
            info.meta_set("youtube_id", v.id.c_str());
            info.meta_set("url", ItemUrl(v).c_str());
            if (v.duration_sec > 0) info.set_length((double)v.duration_sec);
            hints->add_hint(handle, info, filestats_invalid, true);
            any_hints = true;
        }
    }
    if (items.get_count() == 0) return 0;
    if (any_hints) hints->on_done();

    auto pm = playlist_manager::get();
    t_size playlist;
    if (playlist_name) {
        playlist = pm->create_playlist(*playlist_name ? playlist_name : "YouTube Playlist", SIZE_MAX, SIZE_MAX);
    } else {
        playlist = pm->get_active_playlist();
        if (playlist == SIZE_MAX || (pm->playlist_lock_get_filter_mask(playlist) & playlist_lock::filter_add) != 0) {
            playlist = pm->find_or_create_playlist_unlocked("YouTube");
        }
    }
    if (playlist == SIZE_MAX) return 0;

    t_size insert_idx = pm->playlist_get_item_count(playlist);
    if (!pm->playlist_add_items(playlist, items, pfc::bit_array_false())) {
        return 0;
    }

    if (play_now || playlist_name) {
        pm->set_active_playlist(playlist);
    }
    if (play_now) {
        pm->playlist_set_focus_item(playlist, insert_idx);
        pm->playlist_set_selection_single(playlist, insert_idx, true);
        // Same as double-clicking the item in the playlist view.
        if (!pm->playlist_execute_default_action(playlist, insert_idx)) {
            pm->set_playing_playlist(playlist);
            playback_control::get()->start(playback_control::track_command_play);
        }
    }
    return items.get_count();
}

constexpr UINT WM_SEARCH_FINISHED = WM_USER + 101;
// wParam: 1 if anything was added; lParam: std::wstring* status text (receiver deletes).
constexpr UINT WM_EXPAND_DONE = WM_USER + 102;

void PostStatus(HWND hwnd, bool ok, std::wstring text) {
    if (!hwnd || !IsWindow(hwnd)) return;
    auto* msg = new std::wstring(std::move(text));
    if (!PostMessageW(hwnd, WM_EXPAND_DONE, ok ? 1 : 0, (LPARAM)msg)) delete msg;
}

// Resolves links in the background (playlists and channels need many requests) and adds the
// results from the main thread. Single videos go into the active playlist; each playlist or
// channel gets its own new playlist. Tracks are still added if `notify` is closed meanwhile.
void StartExpandJob(std::vector<std::string> urls, bool play_now, HWND notify) {
    std::thread([urls = std::move(urls), play_now, notify]() {
        struct Group {
            std::string name;
            bool is_list = false;
            std::vector<YtItem> videos;
        };
        std::vector<Group> groups;
        Group singles;
        std::string errors;

        for (const auto& url : urls) {
            int kind = ytpipe_classify_url(url.c_str());
            if (kind == YTPIPE_KIND_VIDEO) {
                char id[64] = {0};
                if (ytpipe_extract_video_id(url.c_str(), id, sizeof(id))) {
                    YtItem v;
                    v.id = id;
                    singles.videos.push_back(std::move(v));
                }
                continue;
            }
            if (kind == 0) {
                errors += url + ": not a YouTube link\n";
                continue;
            }
            Group g;
            g.is_list = true;
            char title[512] = {0};
            char err[512] = {0};
            int n = ytpipe_expand_url(url.c_str(), kMaxExpandItems, ItemCollector, &g.videos,
                                      title, sizeof(title), err, sizeof(err));
            if (n < 0) {
                errors += url + ": " + err + "\n";
                continue;
            }
            g.name = title;
            groups.push_back(std::move(g));
        }
        if (!singles.videos.empty()) groups.insert(groups.begin(), std::move(singles));

        fb2k::inMainThread([groups = std::move(groups), errors = std::move(errors), play_now, notify]() {
            if (core_api::is_shutting_down()) return;
            size_t total = 0;
            bool play = play_now;
            std::string last_name;
            for (const auto& g : groups) {
                size_t added = AddVideos(g.videos, play, g.is_list ? g.name.c_str() : nullptr);
                if (added > 0) play = false;
                total += added;
                if (g.is_list) last_name = g.name;
            }

            if (!errors.empty()) {
                console::print((PFC_string_formatter() << "[foo_ytpipe] Some links could not be loaded:\n" << errors.c_str()).c_str());
            }

            std::wstring status;
            if (total == 0) {
                status = errors.empty() ? L"Nothing was added." : L"Could not load: " + Utf8ToWide(errors.substr(0, errors.find('\n')));
            } else {
                status = std::wstring(play_now ? L"Playing. " : L"") + L"Added " + std::to_wstring(total) + L" track" + (total == 1 ? L"" : L"s");
                if (groups.size() == 1 && !last_name.empty()) status += L" from " + Utf8ToWide(last_name);
                status += L".";
                if (!errors.empty()) status += L" Some links failed; see the console.";
            }

            if (notify && IsWindow(notify)) {
                PostStatus(notify, total > 0, status);
            } else if (total == 0 && !errors.empty()) {
                popup_message::g_show(errors.c_str(), "YouTube");
            }
        });
    }).detach();
}

// Window IDs for Search Dialog
constexpr int IDC_QUERY_LABEL = 1001;
constexpr int IDC_QUERY_EDIT = 1002;
constexpr int IDC_SEARCH_BTN = 1003;
constexpr int IDC_RESULTS_LABEL = 1004;
constexpr int IDC_RESULTS_LIST = 1005;
constexpr int IDC_PLAY_BTN = 1006;
constexpr int IDC_ADD_BTN = 1007;
constexpr int IDC_STATUS_LABEL = 1008;
constexpr int IDC_CLOSE_BTN = 1009;
constexpr int IDC_TYPE_LABEL = 1010;
constexpr int IDC_TYPE_COMBO = 1011;
constexpr int IDC_MORE_BTN = 1012;

// Window IDs for Add URL Dialog
constexpr int IDC_URL_LABEL = 2001;
constexpr int IDC_URL_EDIT = 2002;
constexpr int IDC_URL_PLAY_BTN = 2003;
constexpr int IDC_URL_ADD_BTN = 2004;
constexpr int IDC_URL_CANCEL_BTN = 2005;
constexpr int IDC_URL_STATUS = 2006;

constexpr UINT_PTR SUBCLASS_ID_DLGCTRL = 9001;

bool IsButton(HWND h) {
    wchar_t cls[16] = {0};
    GetClassNameW(h, cls, 16);
    return _wcsicmp(cls, L"BUTTON") == 0;
}

// Subclass procedure for all child controls:
// Guarantees TAB / Shift+TAB navigation, ENTER handling, and ESCAPE closing without beeping.
LRESULT CALLBACK DlgControlSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData
) {
    (void)dwRefData;

    switch (uMsg) {
    case WM_GETDLGCODE: {
        LRESULT code = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        return code | DLGC_WANTTAB | DLGC_WANTARROWS | DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    }

    case WM_CHAR: {
        if (wParam == VK_TAB || wParam == VK_RETURN || wParam == VK_ESCAPE) {
            return 0;
        }
        break;
    }

    case WM_KEYDOWN: {
        HWND hParent = GetParent(hWnd);
        int ctrlId = (int)GetWindowLongPtrW(hWnd, GWLP_ID);

        // Let an open combo box dropdown handle its own Enter/Escape.
        bool dropped = ctrlId == IDC_TYPE_COMBO && SendMessageW(hWnd, CB_GETDROPPEDSTATE, 0, 0);

        if (wParam == VK_TAB) {
            bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HWND hNext = GetNextDlgTabItem(hParent, hWnd, bShift ? TRUE : FALSE);
            if (hNext && hNext != hWnd) {
                SetFocus(hNext);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE && !dropped) {
            PostMessageW(hParent, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wParam == VK_RETURN && !dropped) {
            int target = 0;
            if (ctrlId == IDC_QUERY_EDIT || ctrlId == IDC_TYPE_COMBO) target = IDC_SEARCH_BTN;
            else if (ctrlId == IDC_RESULTS_LIST) target = IDC_PLAY_BTN;
            else if (ctrlId == IDC_URL_EDIT) target = IDC_URL_PLAY_BTN;
            else if (IsButton(hWnd)) target = ctrlId; // Enter activates the focused button
            if (target != 0) {
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(target, BN_CLICKED), (LPARAM)hWnd);
                return 0;
            }
        }
        break;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, DlgControlSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

HWND MakeChild(HWND parent, DWORD exStyle, const wchar_t* cls, const wchar_t* text, DWORD style,
               int x, int y, int w, int h, int id) {
    return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, MyInstance(), nullptr);
}

void SetupControls(const HWND* controls, size_t count, HFONT hFont) {
    for (size_t i = 0; i < count; ++i) {
        SendMessageW(controls[i], WM_SETFONT, (WPARAM)hFont, TRUE);
        SetWindowSubclass(controls[i], DlgControlSubclassProc, SUBCLASS_ID_DLGCTRL, 0);
    }
}

// ---------------------------------------------------------------------------------------------
// Search dialog

class SearchDialogState {
public:
    HWND hwnd = nullptr;
    HWND hQueryEdit = nullptr;
    HWND hTypeCombo = nullptr;
    HWND hSearchBtn = nullptr;
    HWND hResultsList = nullptr;
    HWND hMoreBtn = nullptr;
    HWND hPlayBtn = nullptr;
    HWND hAddBtn = nullptr;
    HWND hCloseBtn = nullptr;
    HWND hStatusLabel = nullptr;

    std::vector<YtItem> results;
    int results_kind = YTPIPE_KIND_VIDEO;
    std::string continuation;
    bool is_searching = false;
};

static SearchDialogState* g_search_state = nullptr;

struct SearchJob {
    std::vector<YtItem> items;
    std::string continuation;
    int result_code = 0;
    bool append = false;
};

int ComboKind(HWND combo) {
    int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (sel == 1) return YTPIPE_KIND_PLAYLIST;
    if (sel == 2) return YTPIPE_KIND_CHANNEL;
    return YTPIPE_KIND_VIDEO;
}

std::wstring ResultLine(size_t index, const YtItem& r) {
    std::wstring line = std::to_wstring(index + 1) + L". ";
    if (r.kind == YTPIPE_KIND_PLAYLIST) line += L"Playlist: ";
    else if (r.kind == YTPIPE_KIND_CHANNEL) line += L"Channel: ";
    line += Utf8ToWide(r.title);
    if (!r.channel.empty()) line += L" by " + Utf8ToWide(r.channel);
    if (!r.info.empty()) line += L" (" + Utf8ToWide(r.info) + L")";
    return line;
}

void UpdateMoreButton(SearchDialogState* st) {
    EnableWindow(st->hMoreBtn, !st->is_searching && !st->continuation.empty());
}

void StartSearchJob(SearchDialogState* st, const std::string& query, int kind, std::string continuation) {
    st->is_searching = true;
    EnableWindow(st->hSearchBtn, FALSE);
    UpdateMoreButton(st);
    bool append = !continuation.empty();
    HWND hwnd = st->hwnd;

    std::thread([query, kind, continuation = std::move(continuation), append, hwnd]() {
        auto* job = new SearchJob();
        job->append = append;
        std::vector<char> next(8192, 0);
        job->result_code = ytpipe_search_page(query.c_str(), kind, continuation.c_str(),
            ItemCollector, &job->items, next.data(), next.size());
        job->continuation = next.data();
        // If the window is gone, nobody will free the job.
        if (!IsWindow(hwnd) || !PostMessageW(hwnd, WM_SEARCH_FINISHED, 0, (LPARAM)job)) {
            delete job;
        }
    }).detach();
}

LRESULT CALLBACK SearchDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        modeless_dialog_manager::g_add(hwnd);
        auto* st = g_search_state;
        st->hwnd = hwnd;

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HWND hQueryLabel = MakeChild(hwnd, 0, L"STATIC", L"Search &Query:", 0, 15, 18, 85, 20, IDC_QUERY_LABEL);
        st->hQueryEdit = MakeChild(hwnd, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 100, 15, 265, 25, IDC_QUERY_EDIT);
        HWND hTypeLabel = MakeChild(hwnd, 0, L"STATIC", L"&Type:", 0, 373, 18, 35, 20, IDC_TYPE_LABEL);
        st->hTypeCombo = MakeChild(hwnd, 0, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 410, 15, 95, 120, IDC_TYPE_COMBO);
        st->hSearchBtn = MakeChild(hwnd, 0, L"BUTTON", L"&Search", WS_TABSTOP | BS_DEFPUSHBUTTON, 510, 15, 100, 26, IDC_SEARCH_BTN);
        HWND hResultsLabel = MakeChild(hwnd, 0, L"STATIC", L"Search &Results:", 0, 15, 52, 120, 20, IDC_RESULTS_LABEL);
        st->hResultsList = MakeChild(hwnd, WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 15, 75, 595, 315, IDC_RESULTS_LIST);
        st->hStatusLabel = MakeChild(hwnd, 0, L"STATIC", L"Ready. Type your query and press Enter or Search.",
            SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 15, 408, 185, 22, IDC_STATUS_LABEL);
        st->hMoreBtn = MakeChild(hwnd, 0, L"BUTTON", L"Load &More", WS_TABSTOP | WS_DISABLED, 205, 404, 95, 28, IDC_MORE_BTN);
        st->hPlayBtn = MakeChild(hwnd, 0, L"BUTTON", L"&Play Selected", WS_TABSTOP, 310, 404, 105, 28, IDC_PLAY_BTN);
        st->hAddBtn = MakeChild(hwnd, 0, L"BUTTON", L"&Add to Playlist", WS_TABSTOP, 425, 404, 115, 28, IDC_ADD_BTN);
        st->hCloseBtn = MakeChild(hwnd, 0, L"BUTTON", L"&Close", WS_TABSTOP, 550, 404, 60, 28, IDC_CLOSE_BTN);

        for (const wchar_t* t : { L"Videos", L"Playlists", L"Channels" }) {
            SendMessageW(st->hTypeCombo, CB_ADDSTRING, 0, (LPARAM)t);
        }
        SendMessageW(st->hTypeCombo, CB_SETCURSEL, 0, 0);

        const HWND focusable[] = { st->hQueryEdit, st->hTypeCombo, st->hSearchBtn, st->hResultsList,
                                   st->hMoreBtn, st->hPlayBtn, st->hAddBtn, st->hCloseBtn };
        SetupControls(focusable, _countof(focusable), hFont);
        for (HWND h : { hQueryLabel, hTypeLabel, hResultsLabel, st->hStatusLabel }) {
            SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        SetFocus(st->hQueryEdit);
        return 0;
    }

    case WM_COMMAND: {
        auto* st = g_search_state;
        if (!st) break;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDOK) {
            int target = (GetFocus() == st->hResultsList) ? IDC_PLAY_BTN : IDC_SEARCH_BTN;
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(target, BN_CLICKED), 0);
            return 0;
        }

        if (id == IDC_SEARCH_BTN) {
            if (st->is_searching) return 0;

            std::string query = WideToUtf8(GetControlText(st->hQueryEdit));
            if (query.find_first_not_of(" \t\r\n") == std::string::npos) {
                SetWindowTextW(st->hStatusLabel, L"Type something to search for.");
                SetFocus(st->hQueryEdit);
                return 0;
            }

            SetWindowTextW(st->hStatusLabel, L"Searching YouTube...");
            SendMessageW(st->hResultsList, LB_RESETCONTENT, 0, 0);
            st->results.clear();
            st->continuation.clear();
            st->results_kind = ComboKind(st->hTypeCombo);
            StartSearchJob(st, query, st->results_kind, std::string());
            return 0;
        }

        if (id == IDC_MORE_BTN) {
            if (st->is_searching || st->continuation.empty()) return 0;
            SetWindowTextW(st->hStatusLabel, L"Loading more results...");
            StartSearchJob(st, std::string(), st->results_kind, st->continuation);
            return 0;
        }

        if (id == IDC_RESULTS_LIST && code == LBN_DBLCLK) {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_PLAY_BTN, BN_CLICKED), 0);
            return 0;
        }

        if (id == IDC_PLAY_BTN || id == IDC_ADD_BTN) {
            int sel = (int)SendMessageW(st->hResultsList, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)st->results.size()) {
                SetWindowTextW(st->hStatusLabel, st->results.empty() ? L"Nothing to play. Search first." : L"Select a result first.");
                return 0;
            }
            const auto& item = st->results[sel];
            bool play_now = (id == IDC_PLAY_BTN);

            if (item.kind != YTPIPE_KIND_VIDEO) {
                std::wstring what = item.kind == YTPIPE_KIND_CHANNEL ? L"channel" : L"playlist";
                SetWindowTextW(st->hStatusLabel, (L"Loading " + what + L": " + Utf8ToWide(item.title) + L"...").c_str());
                StartExpandJob({ ItemUrl(item) }, play_now, hwnd);
                return 0;
            }

            if (AddVideos({ item }, play_now, nullptr) == 0) {
                SetWindowTextW(st->hStatusLabel, L"Could not add the track.");
                return 0;
            }
            std::wstring status = (play_now ? L"Playing: " : L"Added to playlist: ") + Utf8ToWide(item.title);
            SetWindowTextW(st->hStatusLabel, status.c_str());
            return 0;
        }

        if (id == IDC_CLOSE_BTN || id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_SEARCH_FINISHED: {
        auto* job = reinterpret_cast<SearchJob*>(lParam);
        auto* st = g_search_state;
        if (!job) return 0;
        if (!st) {
            delete job;
            return 0;
        }
        st->is_searching = false;
        EnableWindow(st->hSearchBtn, TRUE);

        int result_code = job->result_code;
        bool append = job->append;
        size_t first_new = append ? st->results.size() : 0;
        if (!append) st->results.clear();
        // A failed "load more" keeps the old token so the user can retry.
        if (result_code >= 0) st->continuation = job->continuation;
        for (auto& item : job->items) {
            bool dup = false;
            for (const auto& existing : st->results) {
                if (existing.id == item.id) { dup = true; break; }
            }
            if (!dup) st->results.push_back(std::move(item));
        }
        delete job;
        UpdateMoreButton(st);

        if (!append) SendMessageW(st->hResultsList, LB_RESETCONTENT, 0, 0);
        for (size_t i = first_new; i < st->results.size(); ++i) {
            SendMessageW(st->hResultsList, LB_ADDSTRING, 0, (LPARAM)ResultLine(i, st->results[i]).c_str());
        }

        size_t added = st->results.size() - first_new;
        std::wstring more = st->continuation.empty() ? L"" : L" Load More for more.";
        if (result_code < 0) {
            SetWindowTextW(st->hStatusLabel, append ? L"Could not load more results. Try again." : L"Search failed. Check your connection and try again.");
            if (!append) SetFocus(st->hQueryEdit);
        } else if (st->results.empty()) {
            SetWindowTextW(st->hStatusLabel, L"No results found. Try a different query.");
            SetFocus(st->hQueryEdit);
        } else if (append && added == 0) {
            SetWindowTextW(st->hStatusLabel, L"No more results.");
        } else {
            std::wstring status = append
                ? L"Loaded " + std::to_wstring(added) + L" more, " + std::to_wstring(st->results.size()) + L" total." + more
                : L"Found " + std::to_wstring(st->results.size()) + L" results." + more;
            SetWindowTextW(st->hStatusLabel, status.c_str());
            SendMessageW(st->hResultsList, LB_SETCURSEL, (WPARAM)first_new, 0);
            SetFocus(st->hResultsList);
        }
        return 0;
    }

    case WM_EXPAND_DONE: {
        auto* text = reinterpret_cast<std::wstring*>(lParam);
        if (text && g_search_state) SetWindowTextW(g_search_state->hStatusLabel, text->c_str());
        delete text;
        return 0;
    }

    case DM_GETDEFID: {
        if (g_search_state && GetFocus() == g_search_state->hResultsList) {
            return MAKELRESULT(IDC_PLAY_BTN, DC_HASDEFID);
        }
        return MAKELRESULT(IDC_SEARCH_BTN, DC_HASDEFID);
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        modeless_dialog_manager::g_remove(hwnd);
        delete g_search_state;
        g_search_state = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------------------------
// Add URL dialog

// Split the edit box contents on whitespace/commas so several links can be pasted at once.
std::vector<std::string> ParseLinks(const std::string& text, size_t& invalid_count, bool& needs_network) {
    std::vector<std::string> links;
    invalid_count = 0;
    needs_network = false;
    size_t i = 0;
    while (i < text.size()) {
        size_t start = text.find_first_not_of(" \t\r\n,;", i);
        if (start == std::string::npos) break;
        size_t end = text.find_first_of(" \t\r\n,;", start);
        if (end == std::string::npos) end = text.size();
        std::string token = text.substr(start, end - start);
        int kind = ytpipe_classify_url(token.c_str());
        if (kind == 0) {
            ++invalid_count;
        } else {
            if (kind != YTPIPE_KIND_VIDEO) needs_network = true;
            links.push_back(std::move(token));
        }
        i = end;
    }
    return links;
}

void SetUrlDialogBusy(HWND hwnd, bool busy) {
    for (int id : { IDC_URL_EDIT, IDC_URL_PLAY_BTN, IDC_URL_ADD_BTN }) {
        EnableWindow(GetDlgItem(hwnd, id), !busy);
    }
}

LRESULT CALLBACK AddUrlDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        modeless_dialog_manager::g_add(hwnd);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HWND hLabel = MakeChild(hwnd, 0, L"STATIC",
            L"Enter YouTube video, playlist or channel &URL(s):\n(separate several with spaces)",
            0, 15, 15, 460, 36, IDC_URL_LABEL);
        HWND hEdit = MakeChild(hwnd, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 15, 60, 460, 25, IDC_URL_EDIT);
        HWND hStatus = MakeChild(hwnd, 0, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 15, 106, 150, 20, IDC_URL_STATUS);
        HWND hPlay = MakeChild(hwnd, 0, L"BUTTON", L"&Play Now", WS_TABSTOP | BS_DEFPUSHBUTTON, 170, 100, 95, 28, IDC_URL_PLAY_BTN);
        HWND hAdd = MakeChild(hwnd, 0, L"BUTTON", L"&Add to Playlist", WS_TABSTOP, 275, 100, 115, 28, IDC_URL_ADD_BTN);
        HWND hCancel = MakeChild(hwnd, 0, L"BUTTON", L"&Cancel", WS_TABSTOP, 400, 100, 75, 28, IDC_URL_CANCEL_BTN);

        const HWND controls[] = { hEdit, hPlay, hAdd, hCancel };
        SetupControls(controls, _countof(controls), hFont);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetFocus(hEdit);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK || id == IDC_URL_PLAY_BTN || id == IDC_URL_ADD_BTN) {
            HWND hEdit = GetDlgItem(hwnd, IDC_URL_EDIT);
            if (!IsWindowEnabled(hEdit)) return 0; // already loading
            HWND hStatus = GetDlgItem(hwnd, IDC_URL_STATUS);
            size_t invalid = 0;
            bool needs_network = false;
            auto links = ParseLinks(WideToUtf8(GetControlText(hEdit)), invalid, needs_network);

            if (links.empty()) {
                SetWindowTextW(hStatus, L"Not a valid YouTube link.");
                MessageBeep(MB_ICONWARNING);
                SetFocus(hEdit);
                SendMessageW(hEdit, EM_SETSEL, 0, -1);
                return 0;
            }
            if (invalid > 0) {
                console::print((PFC_string_formatter() << "[foo_ytpipe] Skipped " << (unsigned)invalid << " invalid link(s)").c_str());
            }

            // Enter (IDOK) behaves like the default button: Play Now.
            bool play_now = (id != IDC_URL_ADD_BTN);

            if (!needs_network) {
                std::vector<YtItem> videos;
                for (const auto& l : links) {
                    char vid[64] = {0};
                    if (ytpipe_extract_video_id(l.c_str(), vid, sizeof(vid))) {
                        YtItem v;
                        v.id = vid;
                        videos.push_back(std::move(v));
                    }
                }
                if (AddVideos(videos, play_now, nullptr) == 0) {
                    SetWindowTextW(hStatus, L"Could not add to playlist.");
                    return 0;
                }
                DestroyWindow(hwnd);
                return 0;
            }

            SetUrlDialogBusy(hwnd, true);
            SetWindowTextW(hStatus, L"Loading...");
            SetFocus(GetDlgItem(hwnd, IDC_URL_CANCEL_BTN));
            StartExpandJob(std::move(links), play_now, hwnd);
            return 0;
        }

        if (id == IDC_URL_CANCEL_BTN || id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_EXPAND_DONE: {
        auto* text = reinterpret_cast<std::wstring*>(lParam);
        bool ok = wParam != 0;
        if (ok) {
            delete text;
            DestroyWindow(hwnd);
            return 0;
        }
        SetUrlDialogBusy(hwnd, false);
        if (text) SetWindowTextW(GetDlgItem(hwnd, IDC_URL_STATUS), text->c_str());
        delete text;
        MessageBeep(MB_ICONWARNING);
        SetFocus(GetDlgItem(hwnd, IDC_URL_EDIT));
        return 0;
    }

    case DM_GETDEFID:
        return MAKELRESULT(IDC_URL_PLAY_BTN, DC_HASDEFID);

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        modeless_dialog_manager::g_remove(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterDlgClass(const wchar_t* name, WNDPROC proc) {
    WNDCLASSEXW existing = { sizeof(WNDCLASSEXW) };
    if (GetClassInfoExW(MyInstance(), name, &existing)) return;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = proc;
    wc.hInstance = MyInstance();
    wc.lpszClassName = name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
}

HWND CreateCenteredPopup(const wchar_t* cls, const wchar_t* title, DWORD exStyle, DWORD style,
                         int width, int height, HWND parent) {
    RECT work = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    HMONITOR mon = MonitorFromWindow(parent ? parent : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 2;

    return CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME | exStyle, cls, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | style,
        x, y, width, height, parent, nullptr, MyInstance(), nullptr);
}

} // namespace

void ShowYouTubeSearchDialog(HWND parent) {
    if (g_search_state && IsWindow(g_search_state->hwnd)) {
        if (IsIconic(g_search_state->hwnd)) ShowWindow(g_search_state->hwnd, SW_RESTORE);
        SetForegroundWindow(g_search_state->hwnd);
        SetFocus(g_search_state->hQueryEdit);
        return;
    }

    RegisterDlgClass(L"foo_ytpipe_SearchDialog", SearchDlgProc);
    g_search_state = new SearchDialogState();

    HWND hwnd = CreateCenteredPopup(L"foo_ytpipe_SearchDialog", L"YouTube Search & Stream",
        WS_EX_APPWINDOW, WS_MINIMIZEBOX, 645, 485, parent);
    if (!hwnd) {
        delete g_search_state;
        g_search_state = nullptr;
        return;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

void ShowAddYouTubeURLDialog(HWND parent) {
    RegisterDlgClass(L"foo_ytpipe_AddUrlDialog", AddUrlDlgProc);

    HWND hwnd = CreateCenteredPopup(L"foo_ytpipe_AddUrlDialog", L"Add YouTube URL, Playlist or Channel",
        0, 0, 505, 185, parent);
    if (!hwnd) return;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}
