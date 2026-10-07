# Changelog

## 1.5.4

### New
- **Rotating texts**: a text field can hold several texts separated by `||`, e.g. *First line*
  `%artist% || %title%`. Discord then shows them one after the other - the artist, after a few seconds the title,
  then the artist again, and so on. *Display* tab: "Switch between texts separated by || every (s)" (5 s by default
  and at least 5 s, because Discord takes at most 5 updates in 20 seconds). Works for both lines and both tooltips;
  every new track starts with the first text, and the live preview switches as well
- **"Update installed" window**: after a new version was installed - by the update check or by hand - a window says
  so once (in the plugin's language) and where to find what is new (*About* tab). When the plugin restarted AIMP for
  the update, the window says that too, so nobody wonders why AIMP just restarted. AIMP 3 shows the short notice in
  its display instead
- **Own name instead of "AIMP"**: *General* tab, "Name instead of "AIMP"". Discord shows this text instead of the
  application name - in "Listening to ..." on your profile and, with "Discord status text shows: Activity name", in
  the member list. Placeholders work (e.g. `%artist%` or `%title%`), and so do several texts with `||`. Empty = the
  application's name as before. No own Discord application is needed for this

### Changed
- Less work while music plays: the track's tags are read again only when AIMP reports a new track, after a jump in
  the position and every 3 seconds (titles of internet radio streams) - before, on every check (twice a second on
  Windows)
- Smaller plugin files: built without C++ exception tables and type information (Windows x64 about 7 % smaller)

### Fixed
- Windows AIMP in Wine 10 or newer: the plugin did not find the Linux Discord client when `XDG_RUNTIME_DIR` is not
  `/run/user/<id>`. Newer Wine passes that variable on as `WINE_HOST_XDG_RUNTIME_DIR`; the plugin now reads both

## 1.5.3

### New
- **AIMP 4 and AIMP 3**: the plugin now also supports AIMP 4 - fully, with the settings tab, `.aimppack` installation
  and automatic updates - and, slightly limited, AIMP 3: the presence works, but AIMP 3 has no settings pages and
  cannot install packages, so the plugin is installed by hand and set up in `DiscordRPC.ini`
- **x0.at** as upload host for local covers (no account) - the new default. catbox.moe currently answers uploads
  without an account with nothing, and files uploaded with an account are delivered empty (0 bytes)
- Every uploaded cover is checked before Discord gets the link. If the host fails or delivers nothing, the next
  one stands in (x0.at / catbox.moe) and the failed host is skipped for 30 minutes
- Uploaded covers older than 3 days are checked once per AIMP session; when the host has deleted the file, the
  cover is uploaded / looked up again

- **Covers in Discord are checked**: Discord loads covers through its own media proxy and sometimes shows a "?"
  instead (it then remembers that for the link). A few seconds after sending, the plugin checks Discord's copy of
  the cover. If Discord cannot load it, the same picture is sent again under a new link (up to 2 times), then the
  cover is uploaded / looked up again, and if nothing helps the AIMP logo is shown instead of the "?"
- **Update check**: only the release's `.aimppack` is used (with several, the one named after the plugin). AIMP
  installs it, and as soon as the new plugin file is in place the plugin restarts AIMP so it is loaded (by itself
  AIMP only offers "Restart now"). After the restart the plugin shows "Discord Rich Presence was updated to version
  ..." once

### Changed
- Settings, cover cache and downloads are kept in **AIMP's profile folder**, as the AIMP plugin rules ask. That is
  the same folder as before (`%APPDATA%\AIMP`, Linux `~/.config/AIMP`), except for a portable AIMP: there it is now
  `AIMP\Profile`, and the settings are taken over from the old location once
- Russian / Ukrainian: the *About* tab notes that these translations were made with the help of AI
- Settings that still use catbox.moe (the old default) are switched to x0.at once; catbox.moe can still be chosen
  on the *Cover art* tab

### Fixed
- 32-bit AIMP: the live preview (*Display* tab) and the author picture (*About* tab) stayed empty. AIMP passes the
  drawing area by reference, the SDK's C++ header declared it by value
- AIMP 4.70: closing the preferences showed "Invalid pointer operation" (AIMP 4 frees the settings page itself)
- Windows: "Import..." no longer changes AIMP's current folder

## 1.5.2

### Fixed
- *Advanced* tab: "Export..." and "Import..." did nothing on Windows - no file dialog appeared. They now open the
  usual Windows "Save as" / "Open" dialog (in the plugin's data folder, with a suggested file name)
- catbox.moe upload ("HTTP 200: no answer"): an upload is no longer redirected. Likely cause: Windows turns a
  redirected upload into an empty request, which catbox answers with nothing. The request now also matches catbox's own example
  (`userhash` field, `Accept` header). If an upload still fails, the log shows the HTTP status, the size sent and
  what the server reported (redirect target, server, length), and the cover is looked up online as before

## 1.5.1

### New
- *Cover art* tab: a "Get ID" link next to the Imgur Client-ID field opens the Imgur page where you register an
  application and get your Client-ID
- The changelog on the *About* tab is shown in the plugin's language (English, German, Russian or Ukrainian)

### Changed
- *About* tab: the author picture has rounded corners

### Fixed
- Some fields and buttons were cut off at the right edge of the settings page (e.g. "Check now" and the edit fields
  on the *General* and *Advanced* tabs). All tabs now keep a margin on the right
- *Advanced* tab: "Reconnecting..." stayed on the page after the reconnect had finished. It now shows "Connected."
  as soon as the connection is back, and the note disappears after a few seconds (also for "Send test presence")
- The log no longer repeats the "Language: ..." line
- catbox.moe upload: when it fails, the log now shows what catbox answered, so the cause can be found

## 1.5.0

### New
- **About tab**: author, links to GitHub (releases, report a problem) and the complete changelog - the installed
  version always on top
- **Update check**: looks for a new release on GitHub - at every AIMP start, once a day, once a week or once a month
  (or switched off). New versions are downloaded, checked (SHA-256) and opened in AIMP, which installs them; a new
  version is opened automatically only once. "Check now" and "Install" buttons on the About tab
- **Live preview** on the *Display* tab: shows what Discord will show - the activity card with cover, play / pause
  icon, texts and progress bar, and your entry in the member list - while you type, before you press "Apply".
  When nothing plays an example track is shown; when the presence is hidden the preview tells why
- **Cover preview** on the *Cover art* tab: the current cover and where it comes from (file tags, folder image,
  Deezer, iTunes, ...), with a link to the image
- **Advanced tab**: connection details (channel, application ID, last update), "Send test presence", "Reconnect",
  the recent log, hide the presence for certain playlists, choose the language, export / import the settings.
  The own-Discord-application option moved here
- **Languages**: the plugin follows AIMP's interface language - English, German, Russian and Ukrainian are built
  in; the language can also be chosen on the *Advanced* tab. More languages can be added as `Langs\<name>.lng`
  files without a new build
- New placeholder `%playlist%` (name of the playlist the track was started from)

### Changed
- The *Links* tab is part of the *Display* tab now (6 tabs: General, Display, Cover art, Sources, Advanced, About)
- `%status%` ("Playing" / "Paused") is shown in the plugin's language
- Windows: the settings file is saved as Unicode (UTF-16) - texts in any language (e.g. Cyrillic in your own
  lines or filters) are kept correctly. Old files are converted automatically

### Fixed
- **Linux**: the plugin now runs on practically every distribution of the last ten years (glibc 2.17 or newer:
  Ubuntu 18.04+, Debian 9+, Mint, Fedora, Arch, ...). Earlier builds could need a very new glibc (up to 2.38, e.g.
  Ubuntu 24.04) and then did not load
- Linux: the plugin no longer exports its built-in C++ library to AIMP (only its entry point)
- Leaving AIMP could take several seconds while a cover was being looked up online - running downloads are now
  cancelled at once

### Under the hood
- Less than half the file size of 1.4.1 (Windows x64: 0.5 MB instead of 1.3 MB, Linux: 0.55 MB instead of 1.8 MB)
  and fewer CPU wake-ups: optimized for size, no iostreams / filesystem library, the background threads only wake
  up when there is work
- One settings reader / writer for Windows and Linux; hand edits of the INI file are picked up while AIMP runs on
  both systems
- The tests now also cover the update check (fake GitHub server), the previews (rendered to images), the language
  switch, export / import, the test presence, reconnect and the playlist filter - on Linux and on both Windows
  builds under Wine

## 1.4.1

### Fixed
- **Settings page layout**: in 1.4.0 the controls on every tab were scattered across the page, cut off or not
  visible at all (edit fields and drop-down lists). The controls were not anchored, so AIMP moved them when the
  tab grew to its final size. All controls are now anchored to the top-left corner and keep their position

### Changed
- README: new screenshots of all settings tabs (AIMP's own UI style)

### Under the hood
- The tests now check that every control of the settings page is anchored and has a valid size, so this layout
  bug cannot come back unnoticed

## 1.4.0

### New
- **Settings tab on Linux**: the settings page is now built with AIMP's own UI API (`IAIMPServiceUI`) instead of
  Windows controls. Native AIMP for Linux gets the same *Preferences -> Plugins -> Discord Rich Presence* page as
  Windows (General / Display / Cover art / Online sources / Links). Thanks to DarkDrawKill on the AIMP forum for
  the suggestion
- **Follows the AIMP skin**: the controls are drawn by AIMP, so the page matches the current skin (incl. dark mode)
- **One package for all platforms**: `aimp_discord_rpc.aimppack` contains Windows 32-bit, Windows 64-bit and Linux;
  AIMP picks the right build

### Changed
- "Developer Portal", "Create app" (Spotify) and "Get token" (Discogs) are now links that AIMP opens in the browser
  (works on Linux too)
- "Clear cover cache" shows its confirmation on the page instead of a message box
- The INI file on Linux can still be edited by hand while AIMP is running

### Fixed
- Settings saved while the plugin was (re)connecting to Discord were only applied with the next track change

### Under the hood
- No Win32 UI code left (no comctl32 / uxtheme / shell32 dependency)
- `tools/make_aimppack.py` builds the package; CI builds and tests all platforms and attaches the `.aimppack`
- Tests drive the settings page through a mock of AIMP's UI service (open, edit, Apply, save, presence uses the
  new value) on Linux and on both Windows builds under Wine

## 1.3.0 (compared to 1.1.0)

### New platforms
- **Windows 32-bit**: new x86 build for 32-bit AIMP (the plugin entry point is now exported correctly for 32-bit,
  so AIMP can load it)
- **Linux (native)**: new `aimp_discord_rpc.so` for AIMP for Linux (x86_64)
  - talks to Discord over its Unix socket; native Discord, Flatpak, Snap and Vesktop are found automatically
  - settings in `~/.config/AIMP/DiscordRPC.ini`, created on first start with all options; changes to the file
    are applied while AIMP is running
  - online covers use the system's libcurl (loaded at run time - without it the plugin still works, just without
    online covers)
- **Wine**: the Windows DLLs detect when AIMP runs under Wine and connect to the Linux Discord client directly -
  no bridge program needed

### New features
- **Clickable song title**: clicking the title in Discord opens a YouTube search for artist + title
  (new *Links* tab; your own link with placeholders can be used instead)
- **Works without your own Discord application**: the Application ID field moved to
  *General -> Advanced: use my own Discord application*. By default the built-in application is used
- **PreMiD friendly**: while paused, the presence is hidden by default, so other activities (e.g. PreMiD) are
  shown. "Show Paused status" is still available

### Changed
- Default second line is now `by %artist%` (was `%artist%`)
- Paused option "Clear presence" renamed to "Hide (PreMiD / other activity shows)"
- Removed the *Buttons* tab (Discord never showed the buttons on your own profile) - replaced by the clickable title
- Removed the asset key fields (play / pause / fallback icon); the icons of the built-in application are used
- Settings from 1.1.0 are migrated automatically: an own Application ID is kept and "use my own Discord
  application" is switched on. The paused behaviour is set to "Hide" once on update (can be changed back on the
  *General* tab)
