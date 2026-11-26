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

void ytpipe_invalidate_video(const char* video_id);

#define YTPIPE_FETCH_EXPIRED (-2)

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

#define YTPIPE_KIND_VIDEO 1
#define YTPIPE_KIND_PLAYLIST 2
#define YTPIPE_KIND_CHANNEL 3

/* info: duration text (video), "N videos" (playlist) or subscriber text (channel). */
typedef void (*ytpipe_item_callback_t)(
    int kind,
    const char* id,
    const char* title,
    const char* channel,
    const char* info,
    int duration_sec,
    void* user_data
);

/* One page of search results. Pass NULL/"" continuation for page 1, then the token written to
   out_continuation for the next page (empty when there are no more). Returns count or -1. */
int ytpipe_search_page(
    const char* query,
    int kind,
    const char* continuation,
    ytpipe_item_callback_t callback,
    void* user_data,
    char* out_continuation,
    size_t out_continuation_max
);

/* 0 = not YouTube, 1 = video, 2 = playlist/mix, 3 = channel */
int ytpipe_classify_url(const char* input);

/* Expands a video/playlist/mix/channel link into videos. Returns count or -1 (see out_error). */
int ytpipe_expand_url(
    const char* input,
    int max_items,
    ytpipe_item_callback_t callback,
    void* user_data,
    char* out_title,
    size_t out_title_max,
    char* out_error,
    size_t out_error_max
);

#ifdef __cplusplus
}
#endif
