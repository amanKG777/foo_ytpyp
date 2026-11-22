# foo_ytpipe (YouTube Pipeline Source for foobar2000)

A lightweight, high-performance YouTube playback and stream-scrubbing extension for **foobar2000 v2.0+ (x64)**.

Inspired by the direct pipeline extraction method used in **FukeX**, `foo_ytpipe` bypasses external runtimes like `yt-dlp.exe` and `node.js`. It interacts directly with YouTube's InnerTube pipeline to resolve un-throttled, direct audio streams (AAC M4A and Opus WebM) and feeds them into foobar2000's native HTTP streaming architecture for instant seeking ("scrubbing") via HTTP 206 byte-range requests.

Designed with **100% NVDA screen reader accessibility** in mind.

---

## Key Features

1. **Pipeline Scrubbing (No yt-dlp / No Node.js):**
   - Resolves direct audio streams using a high-speed Rust-based InnerTube pipeline.
   - Streams high-bitrate AAC (itag 140) and Opus (itag 251) directly over HTTPS.
   - Supports instantaneous seeking and scrubbing via foobar2000's native HTTP byte-range request engine.

2. **Full foobar2000 v2.0+ 64-bit Integration:**
   - Custom `filesystem` protocol handler for `youtube://<id>` and `yt://<id>`.
   - Single-track `input` service populating Title, Channel/Artist, and Duration metadata.
   - Built against the official foobar2000 C++20 SDK and MSVC v145.

3. **Accessible UI Dialogs (Screen Reader Friendly):**
   - **YouTube Search Window (`File -> YouTube -> Search YouTube...`):**
     - Search input field, Search button.
     - Accessible Results List with Title, Channel, and Duration.
     - "Play Selected", "Add to Playlist", and "Close" controls.
     - Full keyboard navigation (Tab, Enter, Arrow keys, Escape).
   - **Direct URL Adder (`File -> YouTube -> Add YouTube URL or ID...`):**
     - Accepts standard YouTube watch URLs, short URLs (`youtu.be`), Shorts, or 11-character video IDs.

---

## Project Structure

- `ytpipe_core/`: Rust dynamic library exposing clean C-ABI functions for InnerTube metadata extraction, direct stream resolution, and search.
- `foobar2000/foo_ytpipe/`: Native C++ foobar2000 component implementing the `filesystem`, `input`, and Win32 UI dialogs.
- `foobar2000/`: foobar2000 C++ SDK and shared components.
- `foo_ytpipe.fb2k-component`: Ready-to-install foobar2000 component package.

---

## Building from Source

### Prerequisites
- Visual Studio 2022/2026 with C++ Desktop Development (MSVC v144/v145).
- Rust toolchain (`x86_64-pc-windows-msvc`).
- MSBuild.

### Steps
1. Build the Rust core:
   ```powershell
   cargo build --release --manifest-path "ytpipe_core\Cargo.toml"
   ```
2. Build the foobar2000 component:
   ```powershell
   & MSBuild.exe "foobar2000\foo_ytpipe\foo_ytpipe.vcxproj" /p:Configuration=Release /p:Platform=x64
   ```
3. Package the `.fb2k-component`:
   ```powershell
   Compress-Archive -Path "foobar2000\foo_ytpipe\x64\Release\foo_ytpipe.dll", "ytpipe_core\target\release\ytpipe_core.dll" -DestinationPath "foo_ytpipe.zip" -Force
   Move-Item "foo_ytpipe.zip" "foo_ytpipe.fb2k-component" -Force
   ```

---

## Installation

1. Double-click `foo_ytpipe.fb2k-component`, or in foobar2000 go to **Preferences > Components > Install...** and select `foo_ytpipe.fb2k-component`.
2. Restart foobar2000.
3. Access the plugin from the menu:
   - **File -> YouTube -> Search YouTube...**
   - **File -> YouTube -> Add YouTube URL or ID...**
