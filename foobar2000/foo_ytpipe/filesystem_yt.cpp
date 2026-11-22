#include "../SDK/foobar2000.h"
#include "ytpipe_api.h"

namespace {

class filesystem_yt : public filesystem {
public:
    bool is_our_path(const char* p_path) override {
        return (_strnicmp(p_path, "youtube://", 10) == 0 ||
                _strnicmp(p_path, "yt://", 5) == 0);
    }

    bool get_canonical_path(const char* p_path, pfc::string_base& p_out) override {
        char id[64] = {0};
        if (!ytpipe_extract_video_id(p_path, id, sizeof(id))) {
            return false;
        }
        p_out = PFC_string_formatter() << "youtube://" << id;
        return true;
    }

    bool get_display_path(const char* p_path, pfc::string_base& p_out) override {
        return get_canonical_path(p_path, p_out);
    }

    bool is_remote(const char* p_src) override {
        (void)p_src;
        return true;
    }

    void open(service_ptr_t<file>& p_out, const char* p_path, t_open_mode p_mode, abort_callback& p_abort) override {
        if ((p_mode & open_mode_mask) != open_mode_read) {
            throw exception_io_denied("Write operations are not supported on YouTube streams");
        }

        p_abort.check();

        char id[64] = {0};
        if (!ytpipe_extract_video_id(p_path, id, sizeof(id))) {
            throw exception_io_not_found("Invalid YouTube video URL or ID");
        }

        char title[512] = {0};
        char channel[256] = {0};
        int duration_sec = 0;
        char stream_url[4096] = {0};

        int ok = ytpipe_get_video_info(
            id,
            title, sizeof(title),
            channel, sizeof(channel),
            &duration_sec,
            stream_url, sizeof(stream_url)
        );

        if (!ok || stream_url[0] == '\0') {
            throw exception_io("Failed to resolve direct audio stream from YouTube pipeline");
        }

        p_abort.check();

        // Delegate to foobar2000's native HTTP/HTTPS filesystem
        filesystem::g_open(p_out, stream_url, p_mode, p_abort);
    }

    void get_stats(const char* p_path, t_filestats& p_stats, bool& p_is_writeable, abort_callback& p_abort) override {
        (void)p_path; (void)p_abort;
        p_is_writeable = false;
        p_stats = filestats_invalid;
    }

    void remove(const char* p_path, abort_callback& p_abort) override {
        (void)p_path; (void)p_abort;
        throw exception_io_denied();
    }

    void move(const char* p_src, const char* p_dst, abort_callback& p_abort) override {
        (void)p_src; (void)p_dst; (void)p_abort;
        throw exception_io_denied();
    }

    void create_directory(const char* p_path, abort_callback& p_abort) override {
        (void)p_path; (void)p_abort;
        throw exception_io_denied();
    }

    void list_directory(const char* p_path, directory_callback& p_out, abort_callback& p_abort) override {
        (void)p_path; (void)p_out; (void)p_abort;
        throw exception_io_denied();
    }

    bool supports_content_types() override {
        return true;
    }
};

static service_factory_single_t<filesystem_yt> g_filesystem_yt_factory;

} // namespace
