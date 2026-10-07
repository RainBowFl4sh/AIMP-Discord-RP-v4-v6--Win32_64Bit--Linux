# Notes for working on this project

AIMP Discord Rich Presence plugin (C++, AIMP SDK 6.00) for AIMP 4, 5 and 6 (AIMP 3 limited) on Windows x86 / x64
and Linux x86_64. GitHub: `RainBowFl4sh/AIMP-Discord-RP-v4-v6--Win32_64Bit--Linux`, forum topic:
https://www.aimp.ru/forum/index.php?topic=78328.0 (section "Plugins").

## Every release / update must follow the AIMP forum rules

Source: https://www.aimp.ru/forum/index.php?topic=32363.0 (plugin section rules + catalog requirements) and
https://www.aimp.ru/forum/index.php?topic=74 (general forum rules). Read them again if anything is unclear.

Plugin itself:
- Settings and all other files in **AIMP's profile folder** (`IAIMPCore::GetPath(AIMP_CORE_PATH_PROFILE)`, see
  `config::SetProfileDir`) - never next to the plugin or in Program Files (UAC).
- Plugin name in Latin letters; the plugin folder has the name of the main DLL (`aimp_discord_rpc`).

Package / attachments (`tools/make_aimppack.py`):
- Archive = folder named like the main DLL: `aimp_discord_rpc\aimp_discord_rpc.dll` (32-bit),
  `aimp_discord_rpc\x64\aimp_discord_rpc.dll` (64-bit); Linux `.so` in both folders. The same archive is the
  `.aimppack` and the catalog zip `aimp_discord_rpc.zip` (no spaces in file names, `_` instead).
- `aimp_discord_rpc.txt` in each folder, catalog format, **Russian and English**: `Назначение:`, `Версия:` (AIMP
  versions), `Name:`, `Version:`, `Author:`, `AuthorContact:`, `Topic:` (forum topic link), `Description:`, then the
  installation steps. Keep its AIMP versions and features in sync with the release.
- Optional catalog picture `aimp_discord_rpc.jpg`: exactly 200 x 150 px, JPG quality 85-90 %.
- Forum attachment limit 4096 KB per file; files over 1 MB should preferably go to a file host (GitHub is fine).

Forum post (`dist/aimp_forum_post_<version>.txt`, BBCode):
- Files only in the **first post** of the topic; on an update replace text and attachments there (duplicate files in
  replies get deleted). A reply may only point to the first post.
- Topic title: plugin name + short description, with SDK tags `[AIMP4] [AIMP5] [AIMP6]` for the supported versions.
- Post order: Name (`Название / Name:`), version (`Версия / Version:`), description **with screenshots**, changes in
  this version, download (attached files). Link the source code (GitHub).
- Screenshots preferably on image hosts (imgbb, imgur, hostingkartinok); raw GitHub links work - use a tag / branch
  that will not change (e.g. `Full-Linux-Support` for 1.4.1, `main` for the current version).
- No CAPS LOCK text, red colour only for important notes, no transliteration, link sources of third-party info.
- License: without a statement the forum assumes BSD 3-Clause.
- Short Russian section for the mostly Russian-speaking forum, marked as AI-translated
  (`перевод выполнен с помощью ИИ`). The Russian and Ukrainian plugin translations are AI-made - keep that note.

## Release checklist

1. Version in `src/version.h`; entries in `CHANGELOG.md` and `langs/changelog.{de,ru,uk}.md` (shown in the plugin).
2. Build Windows x86 / x64 (MinGW, `cmake/mingw-*.cmake`) and the portable Linux `.so`
   (`tools/build_linux_portable.sh`, glibc 2.17).
3. Tests: `tests/run_linux_test.sh` (with the portable `.so`) and `tests/run_wine_test.sh` for x64 and x86; all green.
4. `tools/make_aimppack.py ... --zip aimp_discord_rpc.zip`; release assets: `.aimppack`, `aimp_discord_rpc.zip`,
   the three per-platform zips; GitHub tag `vX.Y.Z` (the update check reads the newest release).
5. README, forum post (rules above), and the user's own screenshots for README / forum.
6. Do not push or release unless the user asks for it.
