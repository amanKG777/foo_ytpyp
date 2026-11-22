#include "../SDK/foobar2000.h"
#include "ui_dialog.h"

namespace {

// GUIDs for menu groups and commands
static const GUID guid_yt_menu_group = { 0xa742f950, 0x93dc, 0x4f2a, { 0x8a, 0x1b, 0x7c, 0x45, 0x9d, 0x2e, 0x81, 0x60 } };
static const GUID guid_yt_cmd_search = { 0xb128e461, 0x7e83, 0x4591, { 0x92, 0x3d, 0x8e, 0x54, 0x1c, 0x3a, 0x9b, 0x71 } };
static const GUID guid_yt_cmd_add_url = { 0xc349d572, 0x8f94, 0x46a2, { 0xa3, 0x4e, 0x9f, 0x65, 0x2d, 0x4b, 0xac, 0x82 } };

static mainmenu_group_popup_factory g_yt_menu_group(
    guid_yt_menu_group,
    mainmenu_groups::file,
    mainmenu_commands::sort_priority_dontcare,
    "YouTube"
);

class mainmenu_commands_yt : public mainmenu_commands {
public:
    enum {
        cmd_search = 0,
        cmd_add_url,
        cmd_total
    };

    t_uint32 get_command_count() override {
        return cmd_total;
    }

    GUID get_command(t_uint32 p_index) override {
        switch (p_index) {
            case cmd_search: return guid_yt_cmd_search;
            case cmd_add_url: return guid_yt_cmd_add_url;
            default: uBugCheck();
        }
    }

    void get_name(t_uint32 p_index, pfc::string_base& p_out) override {
        switch (p_index) {
            case cmd_search: p_out = "Search YouTube..."; break;
            case cmd_add_url: p_out = "Add YouTube URL or ID..."; break;
            default: uBugCheck();
        }
    }

    bool get_description(t_uint32 p_index, pfc::string_base& p_out) override {
        switch (p_index) {
            case cmd_search:
                p_out = "Opens the accessible YouTube Search and Playback window.";
                return true;
            case cmd_add_url:
                p_out = "Adds or plays a YouTube video directly by URL or ID.";
                return true;
            default:
                return false;
        }
    }

    GUID get_parent() override {
        return guid_yt_menu_group;
    }

    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        (void)p_callback;
        HWND parent = core_api::get_main_window();
        switch (p_index) {
            case cmd_search:
                ShowYouTubeSearchDialog(parent);
                break;
            case cmd_add_url:
                ShowAddYouTubeURLDialog(parent);
                break;
        }
    }
};

static mainmenu_commands_factory_t<mainmenu_commands_yt> g_mainmenu_commands_yt_factory;

} // namespace
