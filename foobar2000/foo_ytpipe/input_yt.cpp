#include "../SDK/foobar2000.h"
#include "ytpipe_api.h"

namespace {

// Decoder GUID for YouTube Pipeline Input
static const GUID guid_yt_decoder = { 0x5e21e09c, 0x8a92, 0x4f37, { 0x9b, 0x14, 0x6e, 0x7a, 0x82, 0x3d, 0x15, 0x90 } };

class input_yt : public input_stubs {
public:
    void open(service_ptr_t<file> p_filehint, const char* p_path, t_input_open_reason p_reason, abort_callback& p_abort) {
        (void)p_filehint;
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

        if (!ok) {
            throw exception_io("Failed to fetch YouTube video metadata");
        }

        m_video_id = id;
        m_title = title;
        m_channel = channel;
        m_duration = (double)duration_sec;
        m_stream_url = stream_url;

        p_abort.check();

        if (p_reason == input_open_decode) {
            if (m_stream_url.is_empty()) {
                throw exception_io("No direct stream URL available for video");
            }
            // Open direct stream through foobar2000 native HTTP client
            filesystem::g_open(m_file, m_stream_url.c_str(), filesystem::open_mode_read, p_abort);
            // Open the appropriate input decoder (AAC / Opus / WebM / MP4)
            input_entry::g_open_for_decoding(m_decoder, m_file, m_stream_url.c_str(), p_abort);
            if (m_decoder.is_empty()) {
                throw exception_io("Failed to initialize internal audio decoder for stream");
            }
        }
    }

    unsigned get_subsong_count() {
        return 1;
    }

    t_uint32 get_subsong(unsigned p_index) {
        (void)p_index;
        return 0;
    }

    void get_info(file_info& p_info, abort_callback& p_abort) {
        if (m_decoder.is_valid()) {
            try {
                m_decoder->get_info(0, p_info, p_abort);
            } catch (...) {}
        }
        if (m_title.length() > 0) p_info.meta_set("title", m_title.c_str());
        if (m_channel.length() > 0) p_info.meta_set("artist", m_channel.c_str());
        if (m_duration > 0.0) p_info.set_length(m_duration);
    }

    t_filestats2 get_stats2(uint32_t, abort_callback& p_abort) {
        (void)p_abort;
        return filestats2_invalid;
    }

    void decode_initialize(unsigned p_flags, abort_callback& p_abort) {
        if (m_decoder.is_valid()) {
            m_decoder->initialize(0, p_flags, p_abort);
        }
    }

    bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort) {
        if (m_decoder.is_valid()) {
            return m_decoder->run(p_chunk, p_abort);
        }
        return false;
    }

    void decode_seek(double p_seconds, abort_callback& p_abort) {
        if (m_decoder.is_valid()) {
            m_decoder->seek(p_seconds, p_abort);
        }
    }

    bool decode_can_seek() {
        if (m_decoder.is_valid()) {
            return m_decoder->can_seek();
        }
        return true;
    }

    bool decode_get_dynamic_info(file_info& p_out, double& p_timestamp_delta) {
        if (m_decoder.is_valid()) {
            return m_decoder->get_dynamic_info(p_out, p_timestamp_delta);
        }
        return false;
    }

    bool decode_get_dynamic_info_track(file_info& p_out, double& p_timestamp_delta) {
        if (m_decoder.is_valid()) {
            return m_decoder->get_dynamic_info_track(p_out, p_timestamp_delta);
        }
        return false;
    }

    void decode_on_idle(abort_callback& p_abort) {
        if (m_decoder.is_valid()) {
            m_decoder->on_idle(p_abort);
        }
    }

    void retag(const file_info&, abort_callback&) {
        throw exception_tagging_unsupported();
    }

    void remove_tags(abort_callback&) {
        throw exception_tagging_unsupported();
    }

    static bool g_is_our_content_type(const char* p_content_type) {
        (void)p_content_type;
        return false;
    }

    static bool g_is_our_path(const char* p_path, const char* p_extension) {
        (void)p_extension;
        return (_strnicmp(p_path, "youtube://", 10) == 0 ||
                _strnicmp(p_path, "yt://", 5) == 0);
    }

    static const char* g_get_name() {
        return "YouTube Pipeline Decoder";
    }

    static const GUID g_get_guid() {
        return guid_yt_decoder;
    }

private:
    pfc::string8 m_video_id;
    pfc::string8 m_title;
    pfc::string8 m_channel;
    double m_duration = 0.0;
    pfc::string8 m_stream_url;

    service_ptr_t<file> m_file;
    service_ptr_t<input_decoder> m_decoder;
};

static input_singletrack_factory_t<input_yt> g_input_yt_factory;

} // namespace
