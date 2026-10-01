# AIMP Discord Rich Presence (x64)

Discord Rich Presence plugin for AIMP 5 and 6 (64-bit) with its own settings tab inside AIMP's preferences.

Author: **Fl4sh**

## Features

- Track title / artist / album on your Discord profile with Discord's **native progress bar** ("Listening to AIMP")
- Album cover:
  - the file's own cover (tags MP3 / FLAC / M4A or folder image) is uploaded to **catbox.moe** (no account / key needed) or Imgur (Client-ID)
  - online lookup without upload: **Spotify** (free developer app), **Deezer**, **Apple Music / iTunes**, **Bandcamp**, **Discogs** (free token), **MusicBrainz + Cover Art Archive**
  - artist check against every hit to avoid wrong covers; covers are cached on disk
- AIMP logo as fallback if no cover is found
- Clickable song title: opens a YouTube search for artist + title (own link optional)
- PreMiD friendly: while AIMP is paused its presence is hidden, so your browser activity (PreMiD) is shown instead
- Works out of the box - no Discord application or key needed
- Own settings tab: *Preferences -> Plugins -> Discord Rich Presence* (General / Display / Cover art / Online sources / Links)
- Text templates with placeholders: `%artist% %title% %album% %albumartist% %genre% %year% %track% %filename% %ext% %pos% %dur% %percent% %bar% %status%`
- Play/pause small icon, paused behaviour (show "Paused" / hide / hide after N minutes)
- Internet radio: shows the current song of the stream; can also be hidden completely
- Hide for paths containing given text (e.g. podcasts, audiobooks)
- Auto reconnect when Discord starts later, live connection status, presence is cleared when AIMP closes

## Requirements

- AIMP 5 or 6, **64-bit**
- Discord **desktop app** (the browser version of Discord cannot receive Rich Presence)
- In Discord: *User Settings -> Activity Privacy -> "Share your detected activities with others"* must be on

## Install

Download the latest version from the [Releases](../../releases) page. There are three ways to install it:

### Option 1: AIMP package (easiest)

1. Download `aimp_discord_rpc.aimppack`
2. **Double-click** the file - AIMP opens and installs the plugin automatically
3. Restart AIMP if it asks you to

### Option 2: Install button in AIMP

1. Download `aimp_discord_rpc.aimppack` **or** `aimp_discord_rpc.dll`
2. In AIMP open *Preferences -> Plugins* and click **Install** at the bottom
3. Select the downloaded file and restart AIMP if asked

### Option 3: Manual

1. Create the folder `AIMP\Plugins\aimp_discord_rpc\` (the folder name must match the DLL name)
2. Copy `aimp_discord_rpc.dll` into it
3. Restart AIMP

### After installing

Make sure the plugin is ticked in *Preferences -> Plugins*. As soon as music is playing, Discord shows
"Listening to AIMP". All options are in *Preferences -> Plugins -> Discord Rich Presence*.

**Updating:** simply install the new version the same way - your settings are kept.

## Files

- Settings: `%APPDATA%\AIMP\DiscordRPC.ini`
- Cover cache: `%APPDATA%\AIMP\DiscordRPC_covers.tsv` (can also be cleared in the *Cover art* tab)

## Cover sources

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

The presence looks the same for everyone: Discord loads the public cover URL itself.

## Notes

- Uploaded covers (catbox.moe / Imgur) are reachable by anyone who has the link. Uploading can be turned off in the *Cover art* tab.
- Online lookups match by artist + album / title, so wrong or missing covers are possible for obscure releases.
- Discord limits updates, so changes are combined (max. one update every 2 seconds).
- Embedded covers are not read from Ogg/Opus, WMA and APE files (folder image / online lookup still work).
- While music plays and PreMiD is active at the same time, Discord shows both activities; Discord decides the order.

### Advanced: own Discord application

Normally not needed. Tick *General -> Advanced: use my own Discord application* and enter its Application ID.
The application needs the art assets `aimp`, `play` and `pause` (Developer Portal -> Rich Presence -> Art Assets).

## Build from source

Built and tested against the **AIMP SDK 6.00** (C++ headers). The SDK headers are not part of this repository's license.

**GitHub Actions (no Visual Studio needed):** put the C++ headers of the AIMP SDK (`Sources\Cpp`, including `Helpers`)
into `sdk/`, push, then open *Actions -> Build DLL*. The finished `aimp_discord_rpc.dll` is attached to the run as an artifact.

**Locally:** copy the SDK headers into `sdk/` and run `build.bat` (Visual Studio 2022 with C++ workload + CMake), or:

```
cmake -S . -B build -A x64 -DAIMP_SDK_DIR="C:/path/to/AIMP_SDK/Sources/Cpp"
cmake --build build --config Release
```

Result: `build/Release/aimp_discord_rpc.dll`

**Creating the .aimppack:** an `.aimppack` is simply a ZIP archive containing the plugin, renamed from `.zip` to `.aimppack`.
