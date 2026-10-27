# Contributing

Thanks for wanting to help with **AIMP Discord Rich Presence**!

The plugin is considered **feature complete** since 1.6.2. New releases focus on bug fixes, updates for new
AIMP / SDK versions and **new languages**. Please keep that in mind before you start on a bigger change - open an
issue first and ask.

## Reporting a bug

Open an issue with the **Bug report** form. Most helpful are:

- plugin version (*About* tab) and AIMP version / edition (Windows 32-bit, 64-bit, Linux, Wine)
- Discord client (desktop app, Flatpak, Snap, Vesktop, ...) - note that **Discord in the browser** cannot show Rich
  Presence at all
- the *recent log* from the *Advanced* tab, and what you expected instead

## Translations

Translations are the most welcome contribution.

1. Copy [`langs/english.lng`](langs/english.lng) to a `Langs` folder next to the plugin
   (e.g. `AIMP\Plugins\aimp_discord_rpc\Langs\polish.lng`).
2. Set `Name`, `Code` and `Aliases` in the `[FILE]` section and translate the texts. Keep placeholders like `%1`
   and `\n` as they are.
3. Restart AIMP and check every tab - long texts must not be cut off.
4. Send the file as a pull request (into `langs/`) or attach it to an issue with the **Translation** form.

Corrections of the Russian and Ukrainian texts (made with the help of AI) by native speakers are very welcome, too.

## Code changes

- Build instructions: see [Build from source](README.md#build-from-source) in the README.
- Every change has to work on **Windows 64-bit, Windows 32-bit and Linux** (the portable Linux build for older
  systems included). The GitHub Actions run builds all of them and tests them against a mock AIMP host, a fake
  Discord client and a fake web server.
- Run the tests before you open a pull request:
  - `python3 tests/check_langs.py` - every language file has all texts
  - `bash tests/run_linux_test.sh build` (Linux) and/or `bash tests/run_wine_test.sh build-x64` (Windows DLLs under Wine)
- New texts go into **all** language files (English is the reference; an English text in another file is better
  than a missing one).
- Match the style of the surrounding code: C++17, no exceptions / RTTI, short comments that explain *why*.
- One topic per pull request, with a line for `CHANGELOG.md` if users will notice the change.

## Code of conduct

Please be friendly and patient - see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
