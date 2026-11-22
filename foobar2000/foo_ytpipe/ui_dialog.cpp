#include "../SDK/foobar2000.h"
#include "ui_dialog.h"
#include "ytpipe_api.h"
#include <commctrl.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>

#pragma comment(lib, "comctl32.lib")

namespace {

struct SearchItemUI {
    std::string id;
    std::string title;
    std::string channel;
    std::string duration_str;
    int duration_sec = 0;
};

// Adds track to foobar2000 playlist and optionally starts playback
void AddOrPlayYouTubeTrack(const char* id_or_url, bool play_now) {
    char id[64] = {0};
    if (!ytpipe_extract_video_id(id_or_url, id, sizeof(id))) {
        popup_message::g_show("Invalid YouTube URL or Video ID.", "YouTube Source Error");
        return;
    }

    pfc::string8 path = PFC_string_formatter() << "youtube://" << id;
    metadb_handle_ptr handle;
    metadb::get()->handle_create(handle, make_playable_location(path.c_str(), 0));

    auto pm = playlist_manager::get();
    t_size playlist = pm->get_active_playlist();
    if (playlist == SIZE_MAX) {
        playlist = pm->create_playlist("YouTube", ~0, ~0);
        pm->set_active_playlist(playlist);
    }

    pfc::list_t<metadb_handle_ptr> items;
    items.add_item(handle);

    t_size insert_idx = pm->playlist_get_item_count(playlist);
    pm->playlist_add_items(playlist, items, pfc::bit_array_true());

    if (play_now) {
        pm->set_active_playlist(playlist);
        pm->playlist_set_focus_item(playlist, insert_idx);
        pm->playlist_set_selection_single(playlist, insert_idx, true);
        playback_control::get()->start(playback_control::track_command_settrack, false);
    }
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

// Window IDs for Add URL Dialog
constexpr int IDC_URL_LABEL = 2001;
constexpr int IDC_URL_EDIT = 2002;
constexpr int IDC_URL_PLAY_BTN = 2003;
constexpr int IDC_URL_ADD_BTN = 2004;
constexpr int IDC_URL_CANCEL_BTN = 2005;

constexpr UINT WM_SEARCH_FINISHED = WM_USER + 101;

class SearchDialogState {
public:
    HWND hwnd = nullptr;
    HWND hQueryEdit = nullptr;
    HWND hSearchBtn = nullptr;
    HWND hResultsList = nullptr;
    HWND hPlayBtn = nullptr;
    HWND hAddBtn = nullptr;
    HWND hCloseBtn = nullptr;
    HWND hStatusLabel = nullptr;

    std::vector<SearchItemUI> results;
    std::mutex results_lock;
    bool is_searching = false;
};

static SearchDialogState* g_search_state = nullptr;

void SearchCallbackFn(
    const char* id,
    const char* title,
    const char* channel,
    const char* duration_str,
    int duration_sec,
    void* user_data
) {
    auto* items = reinterpret_cast<std::vector<SearchItemUI>*>(user_data);
    SearchItemUI item;
    item.id = id ? id : "";
    item.title = title ? title : "";
    item.channel = channel ? channel : "";
    item.duration_str = duration_str ? duration_str : "";
    item.duration_sec = duration_sec;
    items->push_back(std::move(item));
}

LRESULT CALLBACK SearchDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreateWindowW(L"STATIC", L"Search &Query:", WS_CHILD | WS_VISIBLE,
            15, 15, 100, 20, hwnd, (HMENU)(INT_PTR)IDC_QUERY_LABEL, GetModuleHandle(nullptr), nullptr);

        g_search_state->hQueryEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            120, 12, 380, 24, hwnd, (HMENU)(INT_PTR)IDC_QUERY_EDIT, GetModuleHandle(nullptr), nullptr);

        g_search_state->hSearchBtn = CreateWindowW(L"BUTTON", L"&Search",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            510, 12, 90, 25, hwnd, (HMENU)(INT_PTR)IDC_SEARCH_BTN, GetModuleHandle(nullptr), nullptr);

        CreateWindowW(L"STATIC", L"Search &Results:", WS_CHILD | WS_VISIBLE,
            15, 45, 120, 20, hwnd, (HMENU)(INT_PTR)IDC_RESULTS_LABEL, GetModuleHandle(nullptr), nullptr);

        g_search_state->hResultsList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            15, 68, 585, 300, hwnd, (HMENU)(INT_PTR)IDC_RESULTS_LIST, GetModuleHandle(nullptr), nullptr);

        g_search_state->hStatusLabel = CreateWindowW(L"STATIC", L"Ready. Enter query and press Search.",
            WS_CHILD | WS_VISIBLE,
            15, 378, 300, 20, hwnd, (HMENU)(INT_PTR)IDC_STATUS_LABEL, GetModuleHandle(nullptr), nullptr);

        g_search_state->hPlayBtn = CreateWindowW(L"BUTTON", L"&Play Selected",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            320, 375, 100, 26, hwnd, (HMENU)(INT_PTR)IDC_PLAY_BTN, GetModuleHandle(nullptr), nullptr);

        g_search_state->hAddBtn = CreateWindowW(L"BUTTON", L"&Add to Playlist",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            425, 375, 110, 26, hwnd, (HMENU)(INT_PTR)IDC_ADD_BTN, GetModuleHandle(nullptr), nullptr);

        g_search_state->hCloseBtn = CreateWindowW(L"BUTTON", L"&Close",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            540, 375, 60, 26, hwnd, (HMENU)(INT_PTR)IDC_CLOSE_BTN, GetModuleHandle(nullptr), nullptr);

        const HWND controls[] = {
            g_search_state->hQueryEdit, g_search_state->hSearchBtn,
            g_search_state->hResultsList, g_search_state->hStatusLabel,
            g_search_state->hPlayBtn, g_search_state->hAddBtn, g_search_state->hCloseBtn
        };
        for (HWND h : controls) {
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        SetFocus(g_search_state->hQueryEdit);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_SEARCH_BTN) {
            if (g_search_state->is_searching) return 0;

            wchar_t wbuf[256] = {0};
            GetWindowTextW(g_search_state->hQueryEdit, wbuf, 256);
            int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
            if (len <= 1) return 0;

            std::string query(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &query[0], len, nullptr, nullptr);
            query.resize(len - 1);

            g_search_state->is_searching = true;
            SetWindowTextW(g_search_state->hStatusLabel, L"Searching YouTube...");
            SendMessageW(g_search_state->hResultsList, LB_RESETCONTENT, 0, 0);

            std::thread([query, hwnd]() {
                auto results = new std::vector<SearchItemUI>();
                ytpipe_search(query.c_str(), 25, SearchCallbackFn, results);
                PostMessageW(hwnd, WM_SEARCH_FINISHED, 0, (LPARAM)results);
            }).detach();

            return 0;
        }

        if (id == IDC_RESULTS_LIST && code == LBN_DBLCLK) {
            SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_PLAY_BTN, BN_CLICKED), 0);
            return 0;
        }

        if (id == IDC_PLAY_BTN || id == IDC_ADD_BTN) {
            int sel = (int)SendMessageW(g_search_state->hResultsList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_search_state->results.size()) {
                const auto& item = g_search_state->results[sel];
                bool play_now = (id == IDC_PLAY_BTN);
                AddOrPlayYouTubeTrack(item.id.c_str(), play_now);

                wchar_t status[256];
                swprintf_s(status, L"%s: %S", play_now ? L"Playing" : L"Added to playlist", item.title.c_str());
                SetWindowTextW(g_search_state->hStatusLabel, status);
            }
            return 0;
        }

        if (id == IDC_CLOSE_BTN || id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_SEARCH_FINISHED: {
        auto* items = reinterpret_cast<std::vector<SearchItemUI>*>(lParam);
        g_search_state->is_searching = false;

        if (items) {
            g_search_state->results = std::move(*items);
            delete items;
        }

        SendMessageW(g_search_state->hResultsList, LB_RESETCONTENT, 0, 0);

        for (size_t i = 0; i < g_search_state->results.size(); ++i) {
            const auto& r = g_search_state->results[i];
            wchar_t line[512];
            swprintf_s(line, L"%zu. %S [%S] (%S)", i + 1, r.title.c_str(), r.channel.c_str(), r.duration_str.c_str());
            SendMessageW(g_search_state->hResultsList, LB_ADDSTRING, 0, (LPARAM)line);
        }

        wchar_t status[128];
        swprintf_s(status, L"Found %zu results.", g_search_state->results.size());
        SetWindowTextW(g_search_state->hStatusLabel, status);

        if (!g_search_state->results.empty()) {
            SendMessageW(g_search_state->hResultsList, LB_SETCURSEL, 0, 0);
            // Focus list for screen readers
            SetFocus(g_search_state->hResultsList);
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        delete g_search_state;
        g_search_state = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Add URL Dialog procedure
LRESULT CALLBACK AddUrlDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreateWindowW(L"STATIC", L"Enter YouTube Video &URL or ID:\n(e.g. https://www.youtube.com/watch?v=... or 11-char ID)",
            WS_CHILD | WS_VISIBLE,
            15, 15, 450, 36, hwnd, (HMENU)(INT_PTR)IDC_URL_LABEL, GetModuleHandle(nullptr), nullptr);

        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            15, 60, 450, 24, hwnd, (HMENU)(INT_PTR)IDC_URL_EDIT, GetModuleHandle(nullptr), nullptr);

        HWND hPlay = CreateWindowW(L"BUTTON", L"&Play Now",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            170, 100, 90, 26, hwnd, (HMENU)(INT_PTR)IDC_URL_PLAY_BTN, GetModuleHandle(nullptr), nullptr);

        HWND hAdd = CreateWindowW(L"BUTTON", L"&Add to Playlist",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            270, 100, 110, 26, hwnd, (HMENU)(INT_PTR)IDC_URL_ADD_BTN, GetModuleHandle(nullptr), nullptr);

        HWND hCancel = CreateWindowW(L"BUTTON", L"&Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            390, 100, 75, 26, hwnd, (HMENU)(INT_PTR)IDC_URL_CANCEL_BTN, GetModuleHandle(nullptr), nullptr);

        const HWND controls[] = { hEdit, hPlay, hAdd, hCancel };
        for (HWND h : controls) {
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        SetFocus(hEdit);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_URL_PLAY_BTN || id == IDC_URL_ADD_BTN) {
            HWND hEdit = GetDlgItem(hwnd, IDC_URL_EDIT);
            wchar_t wbuf[512] = {0};
            GetWindowTextW(hEdit, wbuf, 512);

            int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                std::string url(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &url[0], len, nullptr, nullptr);
                url.resize(len - 1);

                bool play_now = (id == IDC_URL_PLAY_BTN);
                AddOrPlayYouTubeTrack(url.c_str(), play_now);
            }
            DestroyWindow(hwnd);
            return 0;
        }

        if (id == IDC_URL_CANCEL_BTN || id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void ShowYouTubeSearchDialog(HWND parent) {
    if (g_search_state && IsWindow(g_search_state->hwnd)) {
        SetForegroundWindow(g_search_state->hwnd);
        return;
    }

    g_search_state = new SearchDialogState();

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = SearchDlgProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"Foobar2000_YouTubeSearchDialog";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    int width = 630;
    int height = 450;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        L"YouTube Search & Stream (Pipeline)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        x, y, width, height,
        parent, nullptr, GetModuleHandle(nullptr), nullptr
    );

    g_search_state->hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

void ShowAddYouTubeURLDialog(HWND parent) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = AddUrlDlgProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"Foobar2000_YouTubeAddUrlDialog";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    int width = 500;
    int height = 180;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        L"Add YouTube URL or ID",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, width, height,
        parent, nullptr, GetModuleHandle(nullptr), nullptr
    );

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}
