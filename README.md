# AIMP Discord Rich Presence (x64)

Discord Rich Presence plugin for AIMP 5 and 6 (64-bit) with its own settings tab inside AIMP's options dialog.

Author: **Fl4sh**

## Features

- Track title / artist / album on your Discord profile, **native progress bar** ("Listening" activity)
- Album cover:
  - the file's own cover (tags MP3 / FLAC / M4A or folder image) is uploaded to **catbox.moe** (no account / key needed) or Imgur (Client-ID)
  - online lookup without upload: **Spotify** (free developer app), **Deezer**, **Apple Music / iTunes**, **Bandcamp**, **Discogs** (free token), **MusicBrainz + Cover Art Archive**
  - artist check against every hit to avoid wrong covers; covers are cached on disk
- AIMP logo as fallback if no cover is found
- Own settings tab: *Options -> Plugins -> Discord Rich Presence* (General / Display / Cover art / Online sources / Links)
- Text templates with placeholders: `%artist% %title% %album% %albumartist% %genre% %year% %track% %filename% %ext% %pos% %dur% %percent% %bar% %status%`
- Optional text progress bar (`%bar%`) for the "Playing" activity type
- Play/pause small icon, paused behaviour (show / clear / clear after N minutes)
- PreMiD friendly: while AIMP is paused its presence is hidden, so your browser activity (PreMiD) is shown instead
- Clickable song title: opens a YouTube search for artist + title (own link optional)
- Works out of the box - no Discord application or key needed
- Hide for streams or for paths containing given text
- Auto reconnect when Discord starts later, live connection status, presence is cleared when AIMP closes
- UI is English only

## Build

**Easiest (no Visual Studio needed):** push this folder to a *private* GitHub repo, put the AIMP SDK C++ headers into `sdk/`,
then open *Actions -> Build DLL* - the finished `aimp_discord_rpc.dll` is attached to the run as an artifact.

**Locally:** copy the SDK headers into `sdk/` and run `build.bat` (Visual Studio 2022 + CMake).


Requirements: Visual Studio 2022 (x64), CMake 3.20+, AIMP SDK (C++ headers, from the SDK section of the official AIMP site).

```
cmake -S . -B build -A x64 -DAIMP_SDK_DIR="C:/path/to/AIMP_SDK/Sources/Cpp"
cmake --build build --config Release
```

Result: `build/Release/aimp_discord_rpc.dll`

## Install

1. Copy the DLL to `AIMP\Plugins\DiscordRPC\aimp_discord_rpc.dll`
2. AIMP -> Options -> Plugins -> enable it
3. Done - Discord shows "Listening to AIMP" while music is playing.

Advanced (optional): to use your own Discord application, tick *General -> Advanced: use my own Discord application*
and enter its Application ID. It needs the art assets `aimp`, `play` and `pause` (Rich Presence -> Art Assets).

Settings file: `%APPDATA%\AIMP\DiscordRPC.ini`, cover cache: `%APPDATA%\AIMP\DiscordRPC_covers.tsv`

## Notes

- **catbox.moe / Imgur**: uploaded covers are public links. Imgur needs your own free Client-ID; upload can be turned off.
- **Deezer**: matches by artist + album / title, so wrong or missing covers are possible for obscure releases.
- Discord limits updates, so changes are coalesced (max. one update per 2 s).
- Cover sources "tags" and "folder" are only used for the upload (catbox.moe / Imgur), because Discord cannot read local files.
- Not supported for embedded covers: Ogg/Opus, WMA, APE (folder image / online lookup still work).

## SDK notes (this code was written without compiling against the SDK!)

All AIMP SDK usage is in `src/plugin.cpp`. If your SDK revision differs, the compiler will point at one of these places:

| What | Used name |
|---|---|
| Plugin entry | `AIMPPluginGetHeader`, `IAIMPPlugin::{InfoGet, InfoGetCategories, Initialize, Finalize, SystemNotification}` |
| Options frame | `IAIMPOptionsDialogFrame::{GetName, CreateFrame, DestroyFrame, Notification}`, `IAIMPServiceOptionsDialog::FrameModified`, `AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_{LOAD,SAVE}`, registration via `RegisterExtension(IID_IAIMPServiceOptionsDialog, ...)` |
| Player | `IAIMPServicePlayer::GetPlaylistItem`, `IAIMPServiceMessageDispatcher::Send` with `AIMP_MSG_PROPERTY_PLAYER_{STATE,POSITION,DURATION}` |
| Metadata | `AIMP_PLAYLISTITEM_PROPID_{FILEINFO,FILENAME}`, `AIMP_FILEINFO_PROPID_{ARTIST,ALBUMARTIST,TITLE,ALBUM,GENRE,DATE,TRACKNUMBER,DURATION}` |
| Player state values | assumed 0 = stopped, 1 = paused, 2 = playing (`kPlayer*` constants) |

Common fixes:
- *unresolved external `IID_...`*: remove `#define INITGUID` at the top of `plugin.cpp` (or add it if missing).
- *a constant/method does not exist*: replace it with the equivalent from your SDK headers; `FrameModified` can simply be removed (only enables the "Apply" button).
- If the settings page is cut off in AIMP's options window, reduce row spacing in `settings_ui.cpp`.

## Cover sources (1.1)
| Source | Key needed | Notes |
|---|---|---|
| Local cover -> catbox.moe | no | public link, permanent |
| Local cover -> Imgur | Client-ID | |
| Spotify | Client ID + Secret | developer.spotify.com/dashboard |
| Deezer | no | |
| Apple Music / iTunes | no | |
| Bandcamp | no | unofficial endpoint, may change |
| Discogs | personal token | discogs.com/settings/developers |
| MusicBrainz / Cover Art Archive | no | |

The presence looks the same for everyone: Discord fetches the public cover URL itself.
