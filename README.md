# foo_ytpyp: YouTube for foobar2000

foo_ytpyp lets you search YouTube and play it straight from foobar2000, the same way you'd play a file on your own disk. You can play single videos, whole playlists, mixes and every upload from a channel. Seeking is instant, and you don't need yt-dlp, Python, Node.js or a browser.

It's built for screen reader users by a screen reader user. Every window works from the keyboard and reads properly in NVDA.

It runs on **foobar2000 v2.0 or later, 64-bit**.

---

## What you can do with it

### Search YouTube without leaving foobar2000

Open **File > YouTube > Search YouTube...**, type what you're looking for and press Enter.

- Search for **videos**, **playlists** or **channels** using the Type box next to the search field.
- Results are read as "1. Song title by Channel (3:45)", so you hear what matters first.
- Press Enter on a result to play it, or use **Add to Playlist** to queue it up for later.
- Got to the end of the list? **Load More** fetches the next page, and focus jumps to the first new result.
- Pick a playlist or channel result and all of its videos get added.

### Paste any YouTube link

Open **File > YouTube > Add YouTube URL or ID...** and paste a link. Almost anything works:

- Normal video links: `youtube.com/watch?v=...`, `youtu.be/...`, Shorts, live streams, embeds, YouTube Music, mobile links, and links with extra stuff like `&t=30s` or `?si=...`
- Playlists: `youtube.com/playlist?list=...`, including YouTube Music albums and playlists
- Mixes (the auto-generated "Mix - ..." playlists)
- Channels: `youtube.com/@handle`, `/channel/UC...`, `/c/...` or `/user/...`, which load that channel's uploads
- Just the 11-character video ID on its own

You can paste several links at once, separated by spaces. Press Enter to play right away, or choose **Add to Playlist**.

Single videos go into your current playlist. Each playlist or channel gets its own new foobar2000 playlist, named after it. Big playlists load in the background, so you can close the window and they'll still show up. Titles, artists and lengths come in with the list, so you don't get a wall of `youtube://` addresses.

### Play it like a local file

- **Instant seeking.** Jump anywhere in a track and it starts playing from there within a second, even in hour-long mixes.
- **Best audio available.** It picks the highest-quality audio YouTube offers for each video (usually Opus around 160 kbps, or AAC) and plays it through foobar2000's own decoders. Your DSPs, ReplayGain, output settings and visualisations all work as usual.
- **Original language first.** On videos with AI-dubbed audio tracks, you get the original voice, not a dub.
- **Survives long pauses.** YouTube stream links expire after a few hours. If you pause for ages or your PC sleeps, the plugin quietly gets a fresh link and carries on from where you were.
- **Shrugs off network hiccups.** A dropped connection mid-song gets retried instead of cutting the track short.
- **Proper track info.** Title, channel (as the artist), length and codec show up in foobar2000. Each track also gets `youtube_id` and `url` tags, so you can always find your way back to the video.

### Coming from foo_youtube?

Your old playlists keep working. foo_youtube saved tracks as `fy+https://www.youtube.com/watch?v=...`, and foo_ytpyp recognises those and plays them. Uninstall foo_youtube, install this, and your playlists stay as they are.

---

## How it works

All the talking to YouTube is done by **`ytpipe_core`**, a small Rust library that lives in its own repository, [lib_YTPYP](https://github.com/amanKG777/lib_YTPYP), and is pulled in here as a git submodule. See its README for how it gets stream links without yt-dlp, how streaming and seeking work, and the trade-offs involved.

This repository is the foobar2000 part, **`foo_ytpipe`**, written in C++. It plugs the library into foobar2000 as an input (so `youtube://` tracks play like files) and provides the menu items and the two accessible windows.

---

## Privacy

- You don't sign in, and the plugin never asks for your Google account, cookies or passwords.
- It only talks to YouTube's own servers (youtube.com, youtubei.googleapis.com and googlevideo.com). There is no server of ours in between, and no tracking.
- YouTube sees an anonymous app session, nothing tied to you.

---

## Heads up: this can break

YouTube changes its internal workings often and without warning, so this plugin might stop working at any time. When that happens we'll most likely fix it. That's said in good faith, not a promise. If something stops playing, check the foobar2000 console (**View > Console**) for lines starting with `[foo_ytpipe]` and include them when you report it.

---

## Installing

1. Download `foo_ytpyp.fb2k-component`.
2. Double-click it. Or, in foobar2000, go to **Preferences > Components > Install...** and pick the file.
3. Restart foobar2000 when it asks.
4. You'll find everything under **File > YouTube**:
   - **Search YouTube...**
   - **Add YouTube URL or ID...**

Tip: you can give either menu item a keyboard shortcut in **Preferences > Keyboard Shortcuts**.

### Keyboard reference

- **Enter** in the search box searches. On a result, Enter plays it. On a button, Enter presses it.
- **Tab** and **Shift+Tab** move between controls.
- **Alt + underlined letter** jumps straight to a control: Q for Query, T for Type, R for Results, M for Load More, P for Play, A for Add, C for Close.
- **Escape** closes the window.

---

## Building it yourself

### You'll need

- Visual Studio 2022 or newer, with the "Desktop development with C++" workload
- The Rust toolchain for `x86_64-pc-windows-msvc`

### Steps

Clone the repo with submodules:

```powershell
git clone --recurse-submodules https://github.com/amanKG777/foo_ytpyp.git
```

If you already cloned without `--recurse-submodules`, fetch it first:

```powershell
git submodule update --init --recursive
```

Then run these from **PowerShell** or a Visual Studio developer prompt, in the repository folder.

1. Build the Rust library (see the [lib_YTPYP README](https://github.com/amanKG777/lib_YTPYP) for details and its tests):
   ```powershell
   cargo build --release --manifest-path "ytpipe_core\Cargo.toml"
   ```
2. Build the foobar2000 component:
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "foobar2000\foo_ytpipe\foo_ytpipe.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145
   ```
   Change the MSBuild path and `PlatformToolset` to match your Visual Studio version if needed (v143 for VS 2022).
3. Package it:
   ```powershell
   Compress-Archive -Path "foobar2000\foo_ytpipe\x64\Release\foo_ytpipe.dll" -DestinationPath "foo_ytpyp.zip" -Force
   Move-Item "foo_ytpyp.zip" "foo_ytpyp.fb2k-component" -Force
   ```

### Where things are

- `ytpipe_core/` is the [lib_YTPYP](https://github.com/amanKG777/lib_YTPYP) submodule (the Rust core).
- `foobar2000/foo_ytpipe/input_yt.cpp` is the foobar2000 input and the streaming "file".
- `foobar2000/foo_ytpipe/ui_dialog.cpp` contains the Search and Add URL windows.
- `foobar2000/foo_ytpipe/menu.cpp` adds the File > YouTube menu.
- The `foobar2000/`, `pfc/`, `libPPUI/` and `wtl/` folders hold the foobar2000 SDK and its supporting libraries.

---

## Credits

The idea of talking to YouTube's app API directly, instead of shelling out to yt-dlp, comes from [**FukeX**](https://github.com/amankg777/fukex), and the approach to YouTube's app clients follows ideas from **NewPipe**.

---

## License

foo_ytpyp is free and open-source software, released under the **MIT License**. You are free to use, copy, modify and redistribute it, including in commercial projects, as long as the copyright notice and license text are kept. See [`LICENSE`](LICENSE) for the full text.

Copyright © 2026 Aman Kumar Gupta.

### Third-party components

This repository also contains code from other projects, which keep their own licenses:

| Component | License | Where |
|---|---|---|
| [lib_YTPYP](https://github.com/amanKG777/lib_YTPYP) (`ytpipe_core`) | MIT | [`ytpipe_core/LICENSE`](https://github.com/amanKG777/lib_YTPYP/blob/main/LICENSE) |
| foobar2000 SDK | BSD-3-Clause style | [`sdk-license.txt`](sdk-license.txt) |
| pfc | zlib | [`pfc/pfc-license.txt`](pfc/pfc-license.txt) |
| libPPUI | zlib | [`libPPUI/libPPUI-license.txt`](libPPUI/libPPUI-license.txt) |
| WTL (Windows Template Library) | MS-PL | [`wtl/Notes/MS-PL.txt`](wtl/Notes/MS-PL.txt) |

foo_ytpyp is not affiliated with or endorsed by YouTube, Google or the foobar2000 project.
