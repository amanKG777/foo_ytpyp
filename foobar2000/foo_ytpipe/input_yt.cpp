#include "../SDK/foobar2000.h"
#include "ytpipe_api.h"
#include <cstdio>
#include <vector>
#include <algorithm>
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

    // 3. Output to temp debug log file
    char logPath[MAX_PATH] = {0};
    if (GetTempPathA(sizeof(logPath), logPath) > 0) {
        strcat_s(logPath, "ytpipe_debug.log");
        FILE* f = nullptr;
        if (fopen_s(&f, logPath, "a") == 0 && f) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
            fflush(f);
            fclose(f);
        }
    }
}

// YouTube Pipeline Scrubbing Stream (HTTP 206 Partial Content Chunk Stream)
class file_ytpipe : public file_readonly {
public:
    file_ytpipe(const char* stream_url, const char* content_type)
        : m_stream_url(stream_url), m_content_type(content_type), m_position(0), m_total_size(0),
          m_buffer_start(0), m_buffer_len(0)
    {
        // 1 MB buffer for chunk streaming
        m_buffer.resize(1024 * 1024);

        // Pre-fetch first 1 MB chunk (contains WebM/MP4 header and initial audio packets)
        FetchChunkAt(0);
    }

    t_size read(void* p_buffer, t_size p_bytes, abort_callback& p_abort) override {
        p_abort.check();
        if (p_bytes == 0) return 0;
        if (m_total_size > 0 && m_position >= m_total_size) return 0; // EOF

        uint8_t* dst = static_cast<uint8_t*>(p_buffer);
        t_size total_read = 0;

        while (total_read < p_bytes) {
            p_abort.check();
            if (m_total_size > 0 && m_position >= m_total_size) break;

            // Check if current position is inside the buffered chunk
            if (m_position >= m_buffer_start && m_position < m_buffer_start + m_buffer_len) {
                t_size in_buf_offset = static_cast<t_size>(m_position - m_buffer_start);
                t_size in_buf_avail = static_cast<t_size>(m_buffer_len - in_buf_offset);
                t_size to_copy = (std::min)(p_bytes - total_read, in_buf_avail);

                memcpy(dst + total_read, m_buffer.data() + in_buf_offset, to_copy);
                m_position += to_copy;
                total_read += to_copy;
            } else {
                // Fetch next chunk starting at m_position
                if (!FetchChunkAt(m_position)) {
                    break;
                }
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

    bool can_seek() override {
        return true;
    }

    bool get_content_type(pfc::string_base& p_out) override {
        p_out = m_content_type.c_str();
        return true;
    }

    void reopen(abort_callback& p_abort) override {
        seek(0, p_abort);
    }

    bool is_remote() override {
        return true;
    }

private:
    bool FetchChunkAt(t_filesize start_pos) {
        if (m_total_size > 0 && start_pos >= m_total_size) return false;

        t_filesize end_pos = start_pos + m_buffer.size() - 1;
        if (m_total_size > 0 && end_pos >= m_total_size) {
            end_pos = m_total_size - 1;
        }

        uint64_t total = 0;
        int64_t bytes_fetched = ytpipe_fetch_range(
            m_stream_url.c_str(),
            start_pos,
            end_pos,
            m_buffer.data(),
            m_buffer.size(),
            &total
        );

        if (bytes_fetched <= 0) {
            LogDebug((PFC_string_formatter() << "FetchChunkAt failed for range " << start_pos << "-" << end_pos).c_str());
            return false;
        }

        m_buffer_start = start_pos;
        m_buffer_len = static_cast<t_filesize>(bytes_fetched);
        if (total > 0) {
            m_total_size = total;
        }
        return true;
    }

    pfc::string8 m_stream_url;
    pfc::string8 m_content_type;
    t_filesize m_position;
    t_filesize m_total_size;
    t_filesize m_buffer_start;
    t_filesize m_buffer_len;
    std::vector<uint8_t> m_buffer;
};

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
        char mime_type[128] = {0};

        int ok = ytpipe_get_video_info_ex(
            id,
            title, sizeof(title),
            channel, sizeof(channel),
            &duration_sec,
            stream_url, sizeof(stream_url),
            mime_type, sizeof(mime_type)
        );

        LogDebug((PFC_string_formatter() << "ytpipe_get_video_info_ex ok=" << ok << ", id=" << id << ", title=" << title << ", mime=" << mime_type << ", duration=" << duration_sec).c_str());

        if (!ok) {
            LogDebug("ytpipe_get_video_info_ex returned false");
            throw exception_io("Failed to fetch YouTube video metadata or stream URL");
        }

        m_video_id = id;
        m_title = title;
        m_channel = channel;
        m_duration = (double)duration_sec;
        m_stream_url = stream_url;
        m_mime_type = mime_type;

        p_abort.check();

        if (p_reason == input_open_decode) {
            LogDebug("open: handling input_open_decode");
            if (m_stream_url.is_empty()) {
                LogDebug("m_stream_url is empty!");
                throw exception_io("No direct stream URL available for video");
            }

            // 1. Create pipeline scrubbing file stream
            try {
                LogDebug((PFC_string_formatter() << "Creating file_ytpipe for " << m_video_id.c_str()).c_str());
                m_file = new service_impl_t<file_ytpipe>(m_stream_url.c_str(), m_mime_type.c_str());
                LogDebug((PFC_string_formatter() << "file_ytpipe created, size=" << m_file->get_size(p_abort)).c_str());
            } catch (const std::exception& e) {
                LogDebug((PFC_string_formatter() << "Failed to create file_ytpipe: " << e.what()).c_str());
                throw;
            } catch (...) {
                LogDebug("file_ytpipe threw unknown exception");
                throw;
            }

            // 2. Open internal audio decoder (WebM/Opus or MP4/AAC)
            pfc::string8 hint;
            if (strstr(m_mime_type.c_str(), "mp4") != nullptr || strstr(m_mime_type.c_str(), "aac") != nullptr) {
                hint = "dummy.m4a";
            } else {
                hint = "dummy.webm";
            }

            try {
                LogDebug((PFC_string_formatter() << "Opening decoder with hint: " << hint.c_str()).c_str());
                input_entry::g_open_for_decoding(m_decoder, m_file, hint.c_str(), p_abort, true);
                LogDebug((PFC_string_formatter() << "g_open_for_decoding finished, decoder valid=" << m_decoder.is_valid()).c_str());
            } catch (const std::exception& e) {
                LogDebug((PFC_string_formatter() << "g_open_for_decoding threw std::exception: " << e.what()).c_str());
                throw;
            } catch (...) {
                LogDebug("g_open_for_decoding threw non-std exception");
                throw;
            }

            if (m_decoder.is_empty()) {
                LogDebug("m_decoder is empty after g_open_for_decoding!");
                throw exception_io("Failed to initialize audio decoder for stream");
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
        if (m_mime_type.length() > 0) {
            if (strstr(m_mime_type.c_str(), "webm") != nullptr || strstr(m_mime_type.c_str(), "opus") != nullptr) {
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

} // namespace
