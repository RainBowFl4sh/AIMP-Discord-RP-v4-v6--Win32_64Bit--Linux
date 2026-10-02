# Changelog

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
