#!/usr/bin/env python3
"""Builds aimp_discord_rpc.aimppack (a ZIP archive) with all platforms in one package:

    aimp_discord_rpc/
        aimp_discord_rpc.dll        Windows 32-bit
        aimp_discord_rpc.so         Linux x86_64
        aimp_discord_rpc.txt        description in English and Russian (format of the AIMP add-on catalog)
        x64/
            aimp_discord_rpc.dll    Windows 64-bit
            aimp_discord_rpc.so     Linux x86_64 (copy)
            aimp_discord_rpc.txt    the same description

The same file is also the archive for the AIMP add-on catalog (aimp_discord_rpc.zip, --zip): the catalog wants
<name>\<name>.dll, <name>\x64\<name>.dll and a <name>.txt description in each folder, in Russian and English.

AIMP for Windows picks the DLL matching its architecture (x86 at the root, x64 in the x64 folder) and ignores the
.so files. The Linux layout is not documented by AIMP, so the .so is placed in both folders.

Usage: tools/make_aimppack.py --x86 <dll> --x64 <dll> [--linux <so>] [--out aimp_discord_rpc.aimppack] [--zip <catalog zip>]
"""
import argparse
import os
import re
import sys
import zipfile

NAME = "aimp_discord_rpc"
REPO = "https://github.com/RainBowFl4sh/AIMP-Discord-RP-v4-v6--Win32_64Bit--Linux"
TOPIC = "https://www.aimp.ru/forum/index.php?topic=78328.0"


def description(version):
    """<name>.txt as the AIMP add-on catalog describes it: purpose, AIMP versions, the fields, then the installation
    - in Russian and English (the Russian text was translated with AI)."""
    lines = [
        "Назначение: Расширения функционала",
        "Версия: AIMP4, AIMP5, AIMP6 (AIMP3 - с ограничениями / with limitations)",
        "",
        "Name: Discord Rich Presence",
        f"Version: {version}",
        "Author: Fl4sh",
        f"AuthorContact: {REPO}/issues",
        f"Topic: {TOPIC}",
        "Description: Shows the track playing in AIMP as your Discord status (Rich Presence): title, artist, album,"
        " cover art and Discord's live progress bar, with its own settings page, live preview and update check."
        " / Показывает трек, играющий в AIMP, как статус в Discord (Rich Presence): название, исполнитель, альбом,"
        " обложка и полоса прогресса Discord; своя страница настроек, предпросмотр и проверка обновлений.",
        "",
        "English",
        "-------",
        "Installation: double-click aimp_discord_rpc.aimppack (or Preferences -> Plugins -> Install) - AIMP picks the",
        "right build (Windows 32-bit, Windows 64-bit, Linux). By hand: copy the folder aimp_discord_rpc into AIMP's",
        "Plugins folder and restart AIMP. AIMP 3 cannot install packages: copy the folder by hand.",
        "Settings: Preferences -> Plugins -> Discord Rich Presence (AIMP 3: DiscordRPC.ini in AIMP's profile folder).",
        "Needs the Discord desktop app with 'Share your detected activities with others' switched on.",
        f"Source code, changelog and help: {REPO}",
        "",
        "Русский (перевод выполнен с помощью ИИ)",
        "---------------------------------------",
        "Установка: дважды щёлкните aimp_discord_rpc.aimppack (или Настройки -> Плагины -> Установить) - AIMP сам",
        "выберет нужную сборку (Windows 32 бит, Windows 64 бит, Linux). Вручную: скопируйте папку aimp_discord_rpc",
        "в папку Plugins программы AIMP и перезапустите AIMP. AIMP 3 не умеет устанавливать пакеты: скопируйте папку",
        "вручную.",
        "Настройки: Настройки -> Плагины -> Discord Rich Presence (AIMP 3: DiscordRPC.ini в папке профиля AIMP).",
        "Нужно настольное приложение Discord с включённым параметром «Отображать в статусе, во что вы играете»",
        "(Share your detected activities with others).",
        f"Исходный код, список изменений и помощь: {REPO}",
    ]
    return ("\ufeff" + "\r\n".join(lines) + "\r\n").encode("utf-8")


def read_version():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "version.h"), encoding="utf-8") as f:
        m = re.search(r'AIMP_DISCORD_RPC_VERSION\s+"([^"]+)"', f.read())
    return m.group(1) if m else "0.0.0"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--x86", required=True, help="32-bit Windows DLL")
    ap.add_argument("--x64", required=True, help="64-bit Windows DLL")
    ap.add_argument("--linux", help="Linux x86_64 .so (optional)")
    ap.add_argument("--out", default=NAME + ".aimppack")
    ap.add_argument("--zip", help="also write the same archive as the AIMP add-on catalog zip (aimp_discord_rpc.zip)")
    args = ap.parse_args()

    for path in filter(None, [args.x86, args.x64, args.linux]):
        if not os.path.isfile(path):
            sys.exit("missing file: " + path)

    info = description(read_version())
    entries = [
        (args.x86, f"{NAME}/{NAME}.dll"),
        (args.x64, f"{NAME}/x64/{NAME}.dll"),
    ]
    if args.linux:
        entries += [(args.linux, f"{NAME}/{NAME}.so"), (args.linux, f"{NAME}/x64/{NAME}.so")]

    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{NAME}/", b"")
        z.writestr(f"{NAME}/x64/", b"")
        for src, dst in entries:
            z.write(src, dst)
        z.writestr(f"{NAME}/{NAME}.txt", info)
        z.writestr(f"{NAME}/x64/{NAME}.txt", info)
    if args.zip:
        import shutil
        shutil.copyfile(args.out, args.zip)
    print("written", args.out)
    with zipfile.ZipFile(args.out) as z:
        for i in z.infolist():
            print(f"  {i.file_size:>9}  {i.filename}")


if __name__ == "__main__":
    main()
