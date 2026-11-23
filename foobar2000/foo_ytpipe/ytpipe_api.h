#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ytpipe_search_callback_t)(
    const char* id,
    const char* title,
    const char* channel,
    const char* duration_str,
    int duration_sec,
    void* user_data
);

int ytpipe_extract_video_id(
    const char* input,
    char* out_id,
    size_t out_max
);

int ytpipe_get_video_info(
    const char* video_id,
    char* out_title,
    size_t out_title_max,
    char* out_channel,
    size_t out_channel_max,
    int* out_duration_seconds,
    char* out_stream_url,
    size_t out_url_max
);

int ytpipe_get_video_info_ex(
    const char* video_id,
    char* out_title,
    size_t out_title_max,
    char* out_channel,
    size_t out_channel_max,
    int* out_duration_seconds,
    char* out_stream_url,
    size_t out_url_max,
    char* out_mime_type,
    size_t out_mime_max
);

int64_t ytpipe_fetch_range(
    const char* stream_url,
    uint64_t start_byte,
    uint64_t end_byte,
    uint8_t* out_buf,
    size_t buf_capacity,
    uint64_t* out_total_size
);

int ytpipe_search(
    const char* query,
    int max_results,
    ytpipe_search_callback_t callback,
    void* user_data
);

#ifdef __cplusplus
}
#endif
