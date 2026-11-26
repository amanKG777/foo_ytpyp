#include <foobar2000/SDK/foobar2000.h>
#include "ytpipe_api.h"
#include <vector>
#include <algorithm>
#include <exception>

namespace {

// Decoder GUID for YouTube Pipeline Input
static const GUID guid_yt_decoder = { 0x5e21e09c, 0x8a92, 0x4f37, { 0x9b, 0x14, 0x6e, 0x7a, 0x82, 0x3d, 0x15, 0x90 } };

static void LogDebug(const char* msg) {
    OutputDebugStringA("[foo_ytpipe] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

static void LogError(const char* msg) {
    LogDebug(msg);
    console::print((PFC_string_formatter() << "[foo_ytpipe] " << msg).c_str());
}

static bool IsWebmMime(const char* mime) {
    return strstr(mime, "webm") != nullptr || strstr(mime, "opus") != nullptr;
}

struct ResolvedVideo {
    pfc::string8 title;
    pfc::string8 channel;
    int duration_sec = 0;
    pfc::string8 stream_url;
    pfc::string8 mime_type;
};

static bool ResolveVideo(const char* id, ResolvedVideo& out) {
    char title[1024] = {0};
    char channel[512] = {0};
    int duration_sec = 0;
    std::vector<char> stream_url(16384, 0);
    char mime_type[128] = {0};

    int ok = ytpipe_get_video_info_ex(
        id,
        title, sizeof(title),
        channel, sizeof(channel),
        &duration_sec,
        stream_url.data(), stream_url.size(),
        mime_type, sizeof(mime_type)
    );
    if (!ok) return false;

    out.title = title;
    out.channel = channel;
    out.duration_sec = duration_sec;
    out.stream_url = stream_url.data();
    out.mime_type = mime_type;
    return !out.stream_url.is_empty();
}

// YouTube Pipeline Scrubbing Stream (HTTP 206 Partial Content Chunk Stream)
class file_ytpipe : public file_readonly {
public:
    file_ytpipe(const char* video_id, const char* stream_url, const char* content_type, abort_callback& p_abort)
        : m_video_id(video_id), m_stream_url(stream_url), m_content_type(content_type)
    {
        m_buffer.resize(chunk_size);
        // First chunk holds the WebM/MP4 header; failing here must fail the open.
        FetchChunkAt(0, p_abort);
    }

    t_size read(void* p_buffer, t_size p_bytes, abort_callback& p_abort) override {
        p_abort.check();
        uint8_t* dst = static_cast<uint8_t*>(p_buffer);
        t_size total_read = 0;

        while (total_read < p_bytes) {
            if (m_total_size > 0 && m_position >= m_total_size) break; // EOF

            if (m_position >= m_buffer_start && m_position < m_buffer_start + m_buffer_len) {
                t_size in_buf_offset = static_cast<t_size>(m_position - m_buffer_start);
                t_size in_buf_avail = static_cast<t_size>(m_buffer_len - in_buf_offset);
                t_size to_copy = (std::min)(p_bytes - total_read, in_buf_avail);

                memcpy(dst + total_read, m_buffer.data() + in_buf_offset, to_copy);
                m_position += to_copy;
                total_read += to_copy;
            } else {
                // Throws on unrecoverable failure; a silent short read would truncate the track.
                FetchChunkAt(m_position, p_abort);
            }
        }
        return total_read;
    }

    void seek(t_filesize p_position, abort_callback& p_abort) override {
        p_abort.check();
        if (m_total_size > 0 && p_position > m_total_size) {
            throw exception_io_seek_out_of_range();
        }
        m_position = p_position;
    }

    t_filesize get_size(abort_callback& p_abort) override {
        p_abort.check();
        return m_total_size > 0 ? m_total_size : filesize_invalid;
    }

    t_filesize get_position(abort_callback& p_abort) override {
        p_abort.check();
        return m_position;
    }

    bool can_seek() override { return true; }

    bool get_content_type(pfc::string_base& p_out) override {
        p_out = m_content_type.c_str();
        return true;
    }

    void reopen(abort_callback& p_abort) override { seek(0, p_abort); }

    bool is_remote() override { return true; }

private:
    static constexpr size_t chunk_size = 1024 * 1024;
    static constexpr int max_attempts = 4;

    // Re-resolve the stream URL after it expired (long pause, sleep/resume, etc.).
    void RefreshStreamUrl() {
        ytpipe_invalidate_video(m_video_id.c_str());
        ResolvedVideo fresh;
        if (!ResolveVideo(m_video_id.c_str(), fresh)) {
            throw exception_io("YouTube stream URL expired and could not be refreshed");
        }
        // Byte offsets are only meaningful within the same encoded file.
        if (strcmp(fresh.mime_type.c_str(), m_content_type.c_str()) != 0) {
            throw exception_io("YouTube stream format changed after URL refresh");
        }
        m_stream_url = fresh.stream_url;
        m_refreshed_total_check = true;
        LogError((PFC_string_formatter() << "Refreshed expired stream URL for " << m_video_id.c_str()).c_str());
    }

    void FetchChunkAt(t_filesize start_pos, abort_callback& p_abort) {
        if (m_total_size > 0 && start_pos >= m_total_size) {
            throw exception_io_seek_out_of_range();
        }

        t_filesize end_pos = start_pos + m_buffer.size() - 1;
        if (m_total_size > 0 && end_pos >= m_total_size) {
            end_pos = m_total_size - 1;
        }

        bool refreshed = false;
        for (int attempt = 0; attempt < max_attempts; ++attempt) {
            p_abort.check();

            uint64_t total = 0;
            int64_t fetched = ytpipe_fetch_range(
                m_stream_url.c_str(), start_pos, end_pos,
                m_buffer.data(), m_buffer.size(), &total
            );

            if (fetched > 0) {
                if (m_refreshed_total_check) {
                    m_refreshed_total_check = false;
                    if (m_total_size > 0 && total > 0 && total != m_total_size) {
                        throw exception_io("YouTube stream size changed after URL refresh");
                    }
                }
                m_buffer_start = start_pos;
                m_buffer_len = static_cast<t_filesize>(fetched);
                if (total > 0) m_total_size = total;
                return;
            }

            LogDebug((PFC_string_formatter() << "FetchChunkAt " << start_pos << "-" << end_pos
                << " failed (code " << fetched << ", attempt " << (attempt + 1) << ")").c_str());

            if (fetched == YTPIPE_FETCH_EXPIRED && !refreshed) {
                refreshed = true;
                RefreshStreamUrl();
                continue;
            }
            p_abort.sleep(0.5 * (attempt + 1));
        }

        LogError((PFC_string_formatter() << "Giving up reading " << m_video_id.c_str() << " at byte " << start_pos).c_str());
        throw exception_io("Network error while streaming from YouTube");
    }

    pfc::string8 m_video_id;
    pfc::string8 m_stream_url;
    pfc::string8 m_content_type;
    t_filesize m_position = 0;
    t_filesize m_total_size = 0;
    t_filesize m_buffer_start = 0;
    t_filesize m_buffer_len = 0;
    bool m_refreshed_total_check = false;
    std::vector<uint8_t> m_buffer;
};

class input_yt : public input_stubs {
public:
    void open(service_ptr_t<file> p_filehint, const char* p_path, t_input_open_reason p_reason, abort_callback& p_abort) {
        (void)p_filehint;
        p_abort.check();

        if (p_reason == input_open_info_write) {
            throw exception_tagging_unsupported();
        }

        char id[64] = {0};
        if (!ytpipe_extract_video_id(p_path, id, sizeof(id))) {
            throw exception_io_not_found();
        }

        ResolvedVideo info;
        if (!ResolveVideo(id, info)) {
            LogError((PFC_string_formatter() << "Could not resolve video " << id << " (see console/stderr for details)").c_str());
            throw exception_io("Failed to fetch YouTube video metadata or stream URL");
        }

        m_video_id = id;
        m_title = info.title;
        m_channel = info.channel;
        m_duration = (double)info.duration_sec;
        m_stream_url = info.stream_url;
        m_mime_type = info.mime_type;

        p_abort.check();

        if (p_reason == input_open_decode) {
            m_file = new service_impl_t<file_ytpipe>(m_video_id.c_str(), m_stream_url.c_str(), m_mime_type.c_str(), p_abort);

            const char* hint = IsWebmMime(m_mime_type.c_str()) ? "audio.webm" : "audio.m4a";
            try {
                input_entry::g_open_for_decoding(m_decoder, m_file, hint, p_abort, true);
            } catch (const std::exception& e) {
                LogError((PFC_string_formatter() << "Decoder open failed for " << m_video_id.c_str() << ": " << e.what()).c_str());
                throw;
            }

            if (m_decoder.is_empty()) {
                throw exception_io_unsupported_format();
            }
        }
    }

    unsigned get_subsong_count() { return 1; }

    t_uint32 get_subsong(unsigned p_index) {
        (void)p_index;
        return 0;
    }

    void get_info(file_info& p_info, abort_callback& p_abort) {
        bool have_decoder_info = false;
        if (m_decoder.is_valid()) {
            try {
                m_decoder->get_info(0, p_info, p_abort);
                have_decoder_info = true;
            } catch (const std::exception& e) {
                LogDebug((PFC_string_formatter() << "m_decoder->get_info threw: " << e.what()).c_str());
            }
        }
        if (m_title.length() > 0) p_info.meta_set("title", m_title.c_str());
        if (m_channel.length() > 0) p_info.meta_set("artist", m_channel.c_str());
        if (m_video_id.length() > 0) {
            p_info.meta_set("youtube_id", m_video_id.c_str());
            p_info.meta_set("url", (PFC_string_formatter() << "https://www.youtube.com/watch?v=" << m_video_id.c_str()).c_str());
        }
        if (m_duration > 0.0) p_info.set_length(m_duration);
        if (!have_decoder_info && m_mime_type.length() > 0) {
            p_info.info_set("codec", IsWebmMime(m_mime_type.c_str()) ? "Opus" : "AAC");
            p_info.info_set("encoding", "lossy");
        }
    }

    t_filestats2 get_stats2(uint32_t, abort_callback&) {
        return filestats2_invalid;
    }

    void decode_initialize(unsigned p_flags, abort_callback& p_abort) {
        if (m_decoder.is_empty()) throw exception_io("YouTube input was not opened for decoding");
        m_decoder->initialize(0, p_flags, p_abort);
    }

    bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort) {
        if (m_decoder.is_empty()) return false;
        return m_decoder->run(p_chunk, p_abort);
    }

    void decode_seek(double p_seconds, abort_callback& p_abort) {
        if (m_decoder.is_empty()) throw exception_io_object_not_seekable();
        m_decoder->seek(p_seconds, p_abort);
    }

    bool decode_can_seek() {
        return m_decoder.is_valid() && m_decoder->can_seek();
    }

    bool decode_get_dynamic_info(file_info& p_out, double& p_timestamp_delta) {
        return m_decoder.is_valid() && m_decoder->get_dynamic_info(p_out, p_timestamp_delta);
    }

    bool decode_get_dynamic_info_track(file_info& p_out, double& p_timestamp_delta) {
        return m_decoder.is_valid() && m_decoder->get_dynamic_info_track(p_out, p_timestamp_delta);
    }

    void decode_on_idle(abort_callback& p_abort) {
        if (m_decoder.is_valid()) m_decoder->on_idle(p_abort);
    }

    void retag(const file_info&, abort_callback&) { throw exception_tagging_unsupported(); }

    void remove_tags(abort_callback&) { throw exception_tagging_unsupported(); }

    static bool g_is_our_content_type(const char*) { return false; }

    static bool g_is_our_path(const char* p_path, const char* p_extension) {
        (void)p_extension;
        if (!p_path) return false;
        if (_strnicmp(p_path, "youtube://", 10) == 0) return true;
        if (_strnicmp(p_path, "yt://", 5) == 0) return true;
        // Entries left in playlists by the foo_youtube component ("fy+https://www.youtube.com/watch?v=...")
        if (_strnicmp(p_path, "fy+", 3) == 0) p_path += 3;
        // Only claim http(s) URLs pointing at YouTube pages, never googlevideo stream URLs.
        if (_strnicmp(p_path, "http://", 7) != 0 && _strnicmp(p_path, "https://", 8) != 0) return false;
        static const char* const patterns[] = {
            "youtube.com/watch", "youtu.be/", "youtube.com/shorts/",
            "youtube.com/embed/", "youtube.com/live/", "youtube-nocookie.com/embed/",
        };
        for (const char* pat : patterns) {
            if (pfc::string_find_first_ex(p_path, SIZE_MAX, pat, SIZE_MAX) != SIZE_MAX) {
                char id[64];
                return ytpipe_extract_video_id(p_path, id, sizeof(id)) != 0;
            }
        }
        return false;
    }

    static const char* g_get_name() { return "YouTube Pipeline Decoder"; }

    static const GUID g_get_guid() { return guid_yt_decoder; }

private:
    pfc::string8 m_video_id;
    pfc::string8 m_title;
    pfc::string8 m_channel;
    double m_duration = 0.0;
    pfc::string8 m_stream_url;
    pfc::string8 m_mime_type;

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

// Keep playlists created by foo_youtube playable.
class input_protocol_type_fy_https : public input_protocol_type {
public:
    void get_protocol_name(pfc::string_base & out) override { out = "fy+https"; }
    void get_description(pfc::string_base & out) override { out = "YouTube Audio Stream (foo_youtube link)"; }
};
static service_factory_single_t<input_protocol_type_fy_https> g_input_protocol_type_fy_https;

class input_protocol_type_fy_http : public input_protocol_type {
public:
    void get_protocol_name(pfc::string_base & out) override { out = "fy+http"; }
    void get_description(pfc::string_base & out) override { out = "YouTube Audio Stream (foo_youtube link)"; }
};
static service_factory_single_t<input_protocol_type_fy_http> g_input_protocol_type_fy_http;

} // namespace
