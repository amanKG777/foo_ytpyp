#include "../SDK/foobar2000.h"
#include "ytpipe_api.h"
#include <cstdio>
#include <exception>

namespace {

// Decoder GUID for YouTube Pipeline Input
static const GUID guid_yt_decoder = { 0x5e21e09c, 0x8a92, 0x4f37, { 0x9b, 0x14, 0x6e, 0x7a, 0x82, 0x3d, 0x15, 0x90 } };

static void LogDebug(const char* msg) {
    // 1. Output to debugger / Sysinternals DebugView
    OutputDebugStringA("[foo_ytpipe] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");

    // 2. Output to foobar2000 console
    console::print((PFC_string_formatter() << "[foo_ytpipe] " << msg).c_str());

    // 3. Output to local debug log file
    FILE* f = nullptr;
    if (fopen_s(&f, "C:\\Users\\nab5\\scrubYouTube\\ytpipe_debug.log", "a") == 0 && f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fflush(f);
        fclose(f);
    }
}

class input_yt : public input_stubs {
public:
    void open(service_ptr_t<file> p_filehint, const char* p_path, t_input_open_reason p_reason, abort_callback& p_abort) {
        (void)p_filehint;
        p_abort.check();

        LogDebug((PFC_string_formatter() << "open enter: path=" << p_path << ", reason=" << (int)p_reason).c_str());

        char id[64] = {0};
        if (!ytpipe_extract_video_id(p_path, id, sizeof(id))) {
            LogDebug("open: failed to extract video id");
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

        LogDebug((PFC_string_formatter() << "ytpipe_get_video_info ok=" << ok << ", id=" << id << ", title=" << title << ", duration=" << duration_sec).c_str());

        if (!ok) {
            LogDebug("ytpipe_get_video_info returned false");
            throw exception_io("Failed to fetch YouTube video metadata or stream URL");
        }

        m_video_id = id;
        m_title = title;
        m_channel = channel;
        m_duration = (double)duration_sec;
        m_stream_url = stream_url;

        p_abort.check();

        if (p_reason == input_open_decode) {
            LogDebug("open: handling input_open_decode");
            if (m_stream_url.is_empty()) {
                LogDebug("m_stream_url is empty!");
                throw exception_io("No direct stream URL available for video");
            }

            // 1. Open HTTP stream via foobar2000 http_client
            try {
                LogDebug((PFC_string_formatter() << "Opening HTTP stream via http_client with url len=" << m_stream_url.length()).c_str());
                auto client = http_client::get();
                auto req = client->create_request("GET");
                req->add_header("User-Agent", "com.google.ios.youtube/21.03.2 (iPhone16,2; U; CPU iOS 18_7_2 like Mac OS X; en_US)");
                m_file = req->run(m_stream_url.c_str(), p_abort);

                pfc::string8 content_type;
                m_file->get_content_type(content_type);
                LogDebug((PFC_string_formatter() << "http_client SUCCESS: is_valid=" << m_file.is_valid()
                    << ", can_seek=" << m_file->can_seek()
                    << ", content_type=" << content_type.c_str()
                    << ", size=" << m_file->get_size(p_abort)).c_str());
            } catch (const std::exception& e) {
                LogDebug((PFC_string_formatter() << "http_client threw std::exception: " << e.what()).c_str());
                throw;
            } catch (...) {
                LogDebug("http_client threw non-std exception");
                throw;
            }

            // 2. Open internal audio decoder (WebM/Opus or MP4/AAC)
            pfc::string8 base_hint;
            if (strstr(m_stream_url.c_str(), "webm") != nullptr || strstr(m_stream_url.c_str(), "opus") != nullptr) {
                base_hint = "dummy.webm";
            } else {
                base_hint = "dummy.m4a";
            }

            const char* hint_candidates[] = {
                base_hint.c_str(),
                (PFC_string_formatter() << "file://" << base_hint.c_str()).c_str(),
                (PFC_string_formatter() << "C:\\" << base_hint.c_str()).c_str(),
                m_stream_url.c_str()
            };

            bool opened = false;
            for (const char* hint : hint_candidates) {
                try {
                    LogDebug((PFC_string_formatter() << "Trying input_entry::g_open_for_decoding with hint: " << hint).c_str());
                    input_entry::g_open_for_decoding(m_decoder, m_file, hint, p_abort, true);
                    if (m_decoder.is_valid()) {
                        LogDebug((PFC_string_formatter() << "g_open_for_decoding succeeded with hint: " << hint).c_str());
                        opened = true;
                        break;
                    }
                } catch (const std::exception& e) {
                    LogDebug((PFC_string_formatter() << "g_open_for_decoding with hint " << hint << " threw std::exception: " << e.what()).c_str());
                } catch (...) {
                    LogDebug((PFC_string_formatter() << "g_open_for_decoding with hint " << hint << " threw non-std exception").c_str());
                }
            }

            if (!opened || m_decoder.is_empty()) {
                LogDebug("All decoder hint attempts failed!");
                throw exception_io("Failed to initialize internal audio decoder for YouTube stream");
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
            } catch (const std::exception& e) {
                LogDebug((PFC_string_formatter() << "m_decoder->get_info threw: " << e.what()).c_str());
            } catch (...) {}
        }
        if (m_title.length() > 0) p_info.meta_set("title", m_title.c_str());
        if (m_channel.length() > 0) p_info.meta_set("artist", m_channel.c_str());
        if (m_duration > 0.0) p_info.set_length(m_duration);
        if (m_stream_url.length() > 0) {
            if (strstr(m_stream_url.c_str(), "webm") != nullptr || strstr(m_stream_url.c_str(), "opus") != nullptr) {
                p_info.info_set("codec", "Opus");
                p_info.info_set("encoding", "lossy");
            } else {
                p_info.info_set("codec", "AAC");
                p_info.info_set("encoding", "lossy");
            }
        }
    }

    t_filestats2 get_stats2(uint32_t, abort_callback& p_abort) {
        (void)p_abort;
        return filestats2_invalid;
    }

    void decode_initialize(unsigned p_flags, abort_callback& p_abort) {
        LogDebug((PFC_string_formatter() << "decode_initialize called with flags=" << p_flags).c_str());
        if (m_decoder.is_valid()) {
            try {
                m_decoder->initialize(0, p_flags, p_abort);
                LogDebug("m_decoder->initialize completed successfully");
            } catch (const std::exception& e) {
                LogDebug((PFC_string_formatter() << "m_decoder->initialize threw: " << e.what()).c_str());
                throw;
            } catch (...) {
                LogDebug("m_decoder->initialize threw unknown exception");
                throw;
            }
        } else {
            LogDebug("decode_initialize: m_decoder is NOT valid!");
        }
    }

    bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort) {
        if (m_decoder.is_valid()) {
            try {
                bool ok = m_decoder->run(p_chunk, p_abort);
                static int run_count = 0;
                if (++run_count <= 5 || !ok) {
                    LogDebug((PFC_string_formatter() << "decode_run ok=" << ok << ", samples=" << p_chunk.get_sample_count() << ", srate=" << p_chunk.get_srate() << ", ch=" << p_chunk.get_channels()).c_str());
                }
                return ok;
            } catch (const std::exception& e) {
                LogDebug((PFC_string_formatter() << "m_decoder->run threw std::exception: " << e.what()).c_str());
                throw;
            } catch (...) {
                LogDebug("m_decoder->run threw unknown exception");
                throw;
            }
        }
        LogDebug("decode_run called but m_decoder is not valid!");
        return false;
    }

    void decode_seek(double p_seconds, abort_callback& p_abort) {
        LogDebug((PFC_string_formatter() << "decode_seek called: seconds=" << p_seconds).c_str());
        if (m_decoder.is_valid()) {
            try {
                m_decoder->seek(p_seconds, p_abort);
                LogDebug("m_decoder->seek completed successfully");
            } catch (const std::exception& e) {
                LogDebug((PFC_string_formatter() << "m_decoder->seek threw std::exception: " << e.what()).c_str());
                throw;
            } catch (...) {
                LogDebug("m_decoder->seek threw unknown exception");
                throw;
            }
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
        if (!p_path) return false;
        if (_strnicmp(p_path, "youtube://", 10) == 0) return true;
        if (_strnicmp(p_path, "yt://", 5) == 0) return true;
        if (strstr(p_path, "youtube.com/watch") != nullptr) return true;
        if (strstr(p_path, "youtu.be/") != nullptr) return true;
        if (strstr(p_path, "youtube.com/shorts/") != nullptr) return true;
        return false;
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

static input_singletrack_factory_t<input_yt, input_entry::flag_redirect | input_entry::flag_parallel_reads_slow> g_input_yt_factory;

class input_protocol_type_youtube : public input_protocol_type {
public:
    void get_protocol_name(pfc::string_base & out) override { out = "youtube"; }
    void get_description(pfc::string_base & out) override { out = "YouTube Audio Stream"; }
};
static service_factory_single_t<input_protocol_type_youtube> g_input_protocol_type_youtube;

class input_protocol_type_yt : public input_protocol_type {
public:
    void get_protocol_name(pfc::string_base & out) override { out = "yt"; }
    void get_description(pfc::string_base & out) override { out = "YouTube Audio Stream (short)"; }
};
static service_factory_single_t<input_protocol_type_yt> g_input_protocol_type_yt;

} // namespace
