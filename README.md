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

## How it works (and how it's different from yt-dlp)

Most YouTube players for desktop apps run **yt-dlp** behind the scenes. yt-dlp is great, but it's a big separate program. It needs regular updates, and because of the way YouTube scrambles its links, it now needs a JavaScript runtime too. foo_ytpyp takes a much shorter path.

### Getting a playable link

When you play a video, YouTube has to hand over a link to the actual audio file on its servers (googlevideo.com). How that link comes back depends on who's asking:

- **The YouTube website** gets links that are scrambled. Unscrambling them means downloading YouTube's player JavaScript and running parts of it. That's most of what yt-dlp does, and it's why yt-dlp breaks whenever YouTube changes that code.
- **YouTube's own apps** (iPhone, Vision Pro, Android VR headsets) talk to YouTube through its internal app API, called InnerTube. For many of these apps, YouTube sends back plain, ready-to-use links.

foo_ytpyp asks the way those apps do. It first gets a normal anonymous visitor session from YouTube, just like a freshly installed app would. Then it requests the video as the Vision Pro app. If that doesn't work, it tries the iPhone app and then the Android VR app. Whichever answers first wins. No JavaScript is run and nothing is unscrambled. It's one small web request, usually well under a second.

### Streaming instead of downloading

yt-dlp-based players usually download the whole audio file, or pipe it through another program, before or while it plays. foo_ytpyp never downloads the whole thing. It reads the audio in 1 MB pieces, asking YouTube's servers for "bytes 5,000,000 to 6,000,000" and so on (this is called an HTTP range request). foobar2000 sees what looks like an ordinary file and decodes it itself.

That's why seeking is instant. Jumping to 45:00 in a mix just means asking for the piece at that spot, not waiting for everything before it to arrive.

### Search, playlists and channels

These use the same InnerTube API, this time asking the way the website does. Search results, playlist contents and channel uploads aren't scrambled, so no extra work is needed. Long playlists and channels come in pages of about 100, and the plugin keeps asking for the next page until it has the lot (up to 5,000 videos).

### What's inside

- **`ytpipe_core`** is a small library written in Rust. It does all the talking to YouTube: finding stream links, searching, loading playlists and channels, and fetching audio pieces.
- **`foo_ytpipe`** is the foobar2000 part, written in C++. It plugs the library into foobar2000 as an input (so `youtube://` tracks play like files) and provides the menu items and the two accessible windows.

### The honest trade-off

This only works as long as YouTube keeps handing those app clients plain links. yt-dlp has a big team chasing every YouTube change. This is one small project. If YouTube locks things down, playback may stop working until the plugin is updated. To soften that, there are three different app clients to fall back on, and they can be swapped out quickly when one stops working.

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

## Credits

The idea of talking to YouTube's app API directly, instead of shelling out to yt-dlp, comes from **FukeX**, and the approach to YouTube's app clients follows ideas from **NewPipe**.
