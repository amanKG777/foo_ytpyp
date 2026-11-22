#include "../SDK/foobar2000.h"

DECLARE_COMPONENT_VERSION(
    "YouTube Pipeline Source",
    "1.0.0",
    "YouTube stream resolution and scrubbing component for foobar2000 v2.0+ (x64).\n"
    "Inspired by the FukeX extractor pipeline. Supports direct audio streaming and\n"
    "instant seeking/scrubbing via HTTP range requests without requiring yt-dlp.\n"
    "Full NVDA screen reader accessibility support."
);

VALIDATE_COMPONENT_FILENAME("foo_ytpipe.dll");
FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
