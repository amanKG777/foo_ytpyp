# foo_ytpyp (YouTube Pipeline Source for foobar2000)

A lightweight, high-performance YouTube playback and stream-scrubbing extension for **foobar2000 v2.0+ (x64)**.

Inspired by the direct pipeline extraction method used in **FukeX**, `foo_ytpyp` bypasses external runtimes like `yt-dlp.exe` and `node.js`. It interacts directly with YouTube's InnerTube pipeline to resolve un-throttled, direct audio streams (AAC M4A and Opus WebM) and feeds them into foobar2000's native HTTP streaming architecture for instant seeking ("scrubbing") via HTTP 206 byte-range requests.

Built for screen reader users by a screen reader user.

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

3. **Playlists, Mixes and Channels:**
   - Paste playlist (`/playlist?list=...`), mix (`RD...`) or channel (`/@handle`, `/channel/UC...`, `/c/...`, `/user/...`) links into **Add YouTube URL**; every video is loaded (across all pages) into a new playlist named after it.
   - Several links can be pasted at once, separated by spaces.
   - Track titles, artists and durations from listings are stored up front, so large playlists don't trigger one network lookup per track.

4. **Search:**
   - Search for videos, playlists or channels (Type box), with **Load More** for further pages.
   - Fully keyboard and screen reader accessible (Enter activates the focused button, Escape closes).

5. **Robust Streaming:**
   - Expired stream URLs are re-resolved automatically mid-playback; transient network errors are retried.
   - Prefers the original audio track over auto-dubbed ones.
   - Plays `fy+https://...` entries left in playlists by the foo_youtube component.

---

## Important Notice & Privacy

Before you install and use this component, please note that this might break at any time. We don't guarantee this will keep working indefinitely because YouTube's internal APIs change frequently. Most probably, if this breaks, we'll come with the fixes—this is said in good faith, but is not enforced or guaranteed.

**Privacy & Security:**
- No user account information or credentials are collected.
- No browser cookies or Google sign-in are required.

---

## Project Structure

- `ytpipe_core/`: Rust dynamic library exposing clean C-ABI functions for InnerTube metadata extraction, direct stream resolution, and search.
- `foobar2000/foo_ytpipe/`: Native C++ foobar2000 component implementing the `filesystem`, `input`, and Win32 UI dialogs.
- `foobar2000/`: foobar2000 C++ SDK and shared components.
- `foo_ytpyp.fb2k-component`: Ready-to-install foobar2000 component package.

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
   Run this from PowerShell or a Developer prompt; in Git Bash, Git's own `link.exe` shadows the MSVC linker.
   Unit tests: `cargo test --release`. Live network tests: `cargo test --release -- --include-ignored`.
2. Build the foobar2000 component:
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "foobar2000\foo_ytpipe\foo_ytpipe.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145
   ```
3. Package the `.fb2k-component`:
   ```powershell
   Compress-Archive -Path "foobar2000\foo_ytpipe\x64\Release\foo_ytpipe.dll" -DestinationPath "foo_ytpyp.zip" -Force
   Move-Item "foo_ytpyp.zip" "foo_ytpyp.fb2k-component" -Force
   ```

---

## Installation

1. Double-click `foo_ytpyp.fb2k-component`, or in foobar2000 go to **Preferences > Components > Install...** and select `foo_ytpyp.fb2k-component`.
2. Restart foobar2000.
3. Access the plugin from the menu:
   - **File -> YouTube -> Search YouTube...**
   - **File -> YouTube -> Add YouTube URL or ID...**
