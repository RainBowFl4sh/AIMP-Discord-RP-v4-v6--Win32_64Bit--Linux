# Änderungen

## 1.6.3

### Behoben
- **Cover aus Ogg-/Opus-, WMA-, APE-, WavPack- und anderen Dateien**: Das Cover in ihren Tags wurde nicht gelesen
  (nur bei MP3, FLAC und M4A). Das Plugin fragt dafür jetzt AIMP, das die Tags aller Formate liest, die es abspielt
- **Geschwindigkeit und Tempo im DSP-Manager von AIMP**: Der Fortschrittsbalken in Discord läuft jetzt so schnell,
  wie der Titel spielt (bei 125 % dauert ein 4:00-Titel 3:12). Bisher wurde die Anzeige alle 2 Sekunden neu gesendet,
  weil AIMP Discords Zähler ständig davonlief. Weicht AIMP aus einem anderen Grund ab, wird die ersten Male sofort
  korrigiert, danach höchstens alle 15 Sekunden
- **Update-Prüfung**: Ist GitHubs Limit für Abfragen ohne Konto aufgebraucht (60 pro Stunde, geteilt von allen
  hinter derselben Adresse - VPN, Uni, Firma), wird die neue Version jetzt über den Release-Feed von GitHub gefunden.
  Klappt auch das nicht, sagt der Tab *Über* das, statt „Fehler 403“ zu zeigen

### Geändert
- Tab *Cover*: „Cover in den Tags (MP3 / FLAC / M4A)“ heißt jetzt „Cover in den Tags der Datei“

### Unter der Haube
- *Test-Presence senden* sendet die Zeit wie die normale Anzeige in Millisekunden
- Builds auf GitHub: Zeitlimits und Wiederholungen beim Herunterladen, damit ein hängender Paketserver einen Build
  nicht stundenlang aufhält; neuere Versionen der Checkout-/Artifact-Actions von GitHub

## 1.6.2

### Neu
- Tab *Über*: ein kleiner Link, um das Plugin mit einer Spende zu unterstützen (PayPal) - ganz freiwillig, alles
  funktioniert auch ohne

### Geändert
- **Discord folgt dem Fortschritt in AIMP fast in Echtzeit**: Anfang und Ende des Titels werden in Millisekunden
  statt in ganzen Sekunden gesendet (Discord lag bis zu einer Sekunde daneben, meist dahinter), und die Anzeige wird
  neu gesendet, sobald AIMP eine halbe Sekunde von Discords Zähler abweicht (Puffern, kleine Sprünge)
- Tab *Vorlagen*: Der Hinweis zu `\,` `\(` `\)` `\%` erklärt jedes Zeichen und hat ein Beispiel; `\\` und `\|`
  stehen jetzt auch dabei

### Behoben
- Tab *Vorlagen*: Unter „Presets automatisch wechseln“ stand „c72“ (ein interner Name), bis man das Kästchen
  angeklickt hat

### Unter der Haube
- Die eingebauten Übersetzungen und Changelogs sind komprimiert gespeichert: Die Plugin-Datei ist etwa 80 KB kleiner

## 1.6.1

### Neu
- **Anklickbare Platzhalter**: Eine Leiste über der Live-Vorschau (Tab *Anzeige* und *Vorlagen*) zeigt jeden
  Platzhalter als Button. Sie bleibt stehen, während die Felder scrollen - Felder und Platzhalter sind zusammen zu
  sehen. Ein Klick fügt ihn am Textcursor des zuletzt angeklickten Felds ein, bei Bedarf mit Leerzeichen davor und
  danach - so lässt sich ein Text ganz ohne Tastatur zusammenklicken. Funktionen wie `%IF(A,B,C)` kommen mit ihren
  Kommas, der Cursor steht dann in der Klammer
- Die Platzhalter sind in Gruppen sortiert (Titel und Album, Wiedergabe, Datei und Audio, weitere Tags,
  Funktionen), die Reiter der Leiste wechseln zwischen ihnen - kein Scrollen mehr in einem kleinen Kästchen

### Geändert
- `%samplerate%` zeigt kHz mit Einheit: `44.1kHz`, `48kHz` (bisher `44100`)
- `%bitdepth%` zeigt die Einheit: `16bit` (bisher `16`)
- Vorhandene Texte werden einmalig angepasst: Eine dahinter geschriebene Einheit (`%samplerate%Hz`,
  `%samplerate% kHz`, `%bitdepth% bit`) wird entfernt, damit sie nicht doppelt erscheint. `%IFGreater` und die
  anderen Bedingungen vergleichen weiterhin mit den alten Zahlen (`%IFGreater(%samplerate%,44100,...)` geht weiter)

## 1.6.0

### Neu
- **Erweiterte Vorlagen** (Tab *Anzeige*, standardmäßig aus; ohne Haken bleibt alles wie bisher). Dafür erscheint
  der Tab *Vorlagen* mit mehrzeiligen Feldern für die Texte (Zeilenumbrüche dienen nur der Übersicht) und einer
  Liste aller Platzhalter und Funktionen:
  - mehr Felder - `%comment%` `%composer%` `%playcount%` `%filesize%` `%bitrate%` `%samplerate%` `%codec%` `%bpm%`
    und weitere - und AIMPs Bedingungen `%IF(A,B,C)`, `%IFEqual(A,B,C,D)`, `%IFGreater(A,B,C,D)` und
    `%IFGreaterOrEqual(A,B,C,D)`, auch verschachtelt und in AIMPs Schreibweise (`%Comment` ohne schließendes `%`),
    sodass Vorlagen aus AIMP übernommen werden können. `%playcount%` kommt aus AIMPs Statistik, die AIMP für Titel
    seiner Musikbibliothek führt (GitHub-Issue #4 – danke an [@TheConfusedlol](https://github.com/TheConfusedlol) für die Idee!)
  - **eigene Texte für Pause und Radio-Streams** („Texte für: Wiedergabe / Pause / Radio / Stream“); ein leeres Feld
    nimmt den Text für die Wiedergabe
  - **Presets**: bis zu 10 Textsätze, im Tab *Vorlagen* gespeichert und geladen. Jedes Preset lässt sich auch per
    Tastenkürzel laden: AIMP führt „Preset 1 laden“ bis „Preset 10 laden“ und „Nächstes Preset laden“ unter
    *Einstellungen > Hotkeys* (Gruppe „Discord Rich Presence“), lokal oder global wie jede AIMP-Aktion (auch in
    AIMP 3, dort werden die Presets in der `DiscordRPC.ini` eingetragen)
  - **Presets automatisch wechseln**: bis zu 10 Regeln „wenn *Feld* enthält / ist gleich *Text* -> Preset“ im Tab
    *Vorlagen*, bei jedem Titel geprüft - z. B. Playlist „Podcasts“ -> Preset „Podcast“, Genre „Klassik“ -> Preset
    „Klassik“. Felder: Playlist, Genre, Interpret, Album, Titel, Dateipfad, Kommentar, Codec, Radio/Stream; mehrere
    Texte mit `;` getrennt, Groß-/Kleinschreibung egal. Die erste passende Regel gilt, passt keine, gelten die normalen
    Texte; ein per Hotkey geladenes Preset gilt bis zum nächsten Titel. Der Tab zeigt, welche Regel gerade greift
- **Hotkey „Discord-Anzeige an / aus“** (*Einstellungen > Hotkeys*, Gruppe „Discord Rich Presence“): blendet die
  Anzeige aus und wieder ein, wie der Haken im Tab *Allgemein*
- Neuer Platzhalter **`%next%`**: der nächste Titel aus AIMPs Warteschlange oder Playlist („Interpret - Titel“), auch
  in AIMP 3
- **Buttons**: zwei Buttons unter der Anzeige mit eigener Beschriftung und eigenem Link, Platzhalter gehen (Tab
  *Anzeige*, standardmäßig aus; vorbelegt mit „Lyrics on Genius“ und „Find on Spotify“ für den laufenden Titel).
  Discord zeigt sie nur anderen, nicht im eigenen Profil - die Live-Vorschau zeigt sie
- Tabs *Anzeige* und *Vorlagen*: Die Einstellungen sind scrollbar (Titel-Link und Buttons stehen unten in *Anzeige*),
  die Live-Vorschau bleibt immer sichtbar

## 1.5.4

### Neu
- **Wechselnde Texte**: Ein Textfeld kann mehrere Texte enthalten, getrennt durch `||`, z. B. *Erste Zeile*
  `%artist% || %title%`. Discord zeigt sie dann nacheinander an - den Künstler, nach ein paar Sekunden den Titel,
  dann wieder den Künstler und so weiter. Tab *Anzeige*: „Zwischen Texten mit || wechseln alle (s)“ (Standard 5 s,
  mindestens 5 s, weil Discord höchstens 5 Updates in 20 Sekunden annimmt). Gilt für beide Zeilen und beide
  Tooltips; jeder neue Titel beginnt mit dem ersten Text, und die Live-Vorschau wechselt mit
- **Fenster „Update installiert“**: Nach der Installation einer neuen Version - über den Update-Check oder von Hand -
  meldet ein Fenster das einmalig (in der Sprache des Plugins) und sagt, wo die Neuerungen stehen (Tab *Über*). Hat
  das Plugin AIMP für das Update neu gestartet, steht das ebenfalls darin - so weiß jeder, warum AIMP gerade neu
  gestartet ist. AIMP 3 zeigt stattdessen den kurzen Hinweis in seiner Anzeige
- **Eigener Name statt „AIMP“**: Tab *Allgemein*, „Name statt „AIMP““. Discord zeigt diesen Text statt des
  Anwendungsnamens - bei „Hört ...“ im Profil und, mit „Discord-Statustext zeigt: Aktivitätsname“, in der
  Mitgliederliste. Platzhalter funktionieren (z. B. `%artist%` oder `%title%`), ebenso mehrere Texte mit `||`. Leer =
  wie bisher der Name der Anwendung. Eine eigene Discord-Anwendung ist dafür nicht nötig

### Geändert
- Weniger Arbeit während der Wiedergabe: Die Tags des Titels werden nur noch neu gelesen, wenn AIMP einen neuen Titel
  meldet, nach einem Sprung in der Position und alle 3 Sekunden (Titel von Internetradio-Streams) - vorher bei jeder
  Prüfung (unter Windows zweimal pro Sekunde)
- Kleinere Plugin-Dateien: ohne C++-Ausnahmetabellen und Typinformationen gebaut (Windows x64 etwa 7 % kleiner)

### Behoben
- Windows-AIMP in Wine 10 oder neuer: Das Plugin fand den Linux-Discord-Client nicht, wenn `XDG_RUNTIME_DIR` nicht
  `/run/user/<id>` ist. Neuere Wine-Versionen geben diese Variable als `WINE_HOST_XDG_RUNTIME_DIR` weiter; das Plugin
  liest jetzt beide

## 1.5.3

### Neu
- **AIMP 4 und AIMP 3**: Das Plugin unterstützt jetzt auch AIMP 4 - vollständig, mit Einstellungs-Tab,
  `.aimppack`-Installation und automatischen Updates - und leicht eingeschränkt AIMP 3: Die Anzeige in Discord
  funktioniert, aber AIMP 3 hat keine Einstellungsseiten für Plugins und kann keine Pakete installieren. Das Plugin
  wird dort von Hand installiert und über die `DiscordRPC.ini` eingestellt
- **x0.at** als Upload-Ziel für lokale Cover (ohne Konto) - der neue Standard. catbox.moe beantwortet Uploads ohne
  Konto zurzeit mit nichts, und Dateien, die mit Konto hochgeladen wurden, werden leer (0 Bytes) ausgeliefert
- Jedes hochgeladene Cover wird geprüft, bevor Discord den Link bekommt. Schlägt ein Dienst fehl oder liefert er
  nichts, springt der nächste ein (x0.at / catbox.moe), und der ausgefallene Dienst wird 30 Minuten übersprungen
- Hochgeladene Cover, die älter als 3 Tage sind, werden einmal pro AIMP-Sitzung geprüft; hat der Dienst die Datei
  gelöscht, wird das Cover neu hochgeladen bzw. gesucht

- **Cover in Discord werden geprüft**: Discord lädt Cover über seinen eigenen Bild-Proxy und zeigt manchmal ein „?“
  statt des Bildes (und merkt sich das für diesen Link). Ein paar Sekunden nach dem Senden prüft das Plugin Discords
  Kopie des Covers. Kann Discord es nicht laden, wird dasselbe Bild unter einem neuen Link erneut gesendet (bis zu
  2-mal), danach wird das Cover neu hochgeladen bzw. gesucht, und hilft nichts, erscheint das AIMP-Logo statt des „?“
- **Update-Check**: Es wird nur die `.aimppack` des Releases verwendet (bei mehreren die mit dem Namen des Plugins).
  AIMP installiert sie, und sobald die neue Plugin-Datei bereitliegt, startet das Plugin AIMP neu, damit sie geladen
  wird (AIMP selbst bietet nur „Jetzt neu starten“ an). Nach dem Neustart meldet das Plugin einmalig
  „Discord Rich Presence wurde auf Version ... aktualisiert“

### Geändert
- Einstellungen, Cover-Cache und Downloads liegen im **Profilordner von AIMP**, wie es die AIMP-Plugin-Regeln
  verlangen. Das ist derselbe Ordner wie bisher (`%APPDATA%\AIMP`, Linux `~/.config/AIMP`), außer bei einem
  portablen AIMP: Dort ist es jetzt `AIMP\Profile`, und die Einstellungen werden einmalig vom alten Ort übernommen
- Russisch / Ukrainisch: Der Tab *Über* weist darauf hin, dass diese Übersetzungen mit Hilfe von KI erstellt wurden
- Einstellungen, die noch catbox.moe nutzen (der alte Standard), werden einmalig auf x0.at umgestellt; catbox.moe
  kann im Tab *Cover* weiterhin gewählt werden

### Behoben
- 32-Bit-AIMP: Die Live-Vorschau (Tab *Anzeige*) und das Autorenbild (Tab *Über*) blieben leer. AIMP übergibt den
  Zeichenbereich als Verweis, der C++-Header des SDK deklarierte ihn als Wert
- AIMP 4.70: Beim Schließen der Einstellungen erschien „Invalid pointer operation“ (AIMP 4 gibt die
  Einstellungsseite selbst frei)
- Windows: „Importieren …“ ändert nicht mehr den aktuellen Ordner von AIMP

## 1.5.2

### Behoben
- Tab *Erweitert*: „Exportieren …“ und „Importieren …“ taten unter Windows nichts - es erschien kein Dateidialog.
  Jetzt öffnet sich der normale Windows-Dialog „Speichern unter“ / „Öffnen“ (im Datenordner des Plugins, mit einem
  vorgeschlagenen Dateinamen)
- Upload zu catbox.moe („HTTP 200: no answer“): Ein Upload wird nicht mehr umgeleitet. Wahrscheinliche Ursache:
  Windows macht aus einem umgeleiteten Upload eine leere Anfrage, auf die catbox mit nichts antwortet. Die Anfrage entspricht jetzt
  außerdem dem Beispiel von catbox (Feld `userhash`, Header `Accept`). Schlägt ein Upload trotzdem fehl, zeigt das
  Log den HTTP-Status, die gesendete Größe und was der Server über sich meldet (Umleitungsziel, Server, Länge); das
  Cover wird wie bisher online gesucht

## 1.5.1

### Neu
- Tab *Cover*: Der Link „ID holen“ neben dem Feld Imgur Client-ID öffnet die Imgur-Seite, auf der du eine Anwendung
  registrierst und deine Client-ID bekommst
- Der Changelog im Tab *Über* erscheint in der Sprache des Plugins (Englisch, Deutsch, Russisch oder Ukrainisch)

### Geändert
- Tab *Über*: Das Bild des Autors hat abgerundete Ecken

### Behoben
- Einige Felder und Buttons wurden am rechten Rand der Einstellungsseite abgeschnitten (z. B. „Jetzt suchen“ und
  die Eingabefelder in den Tabs *Allgemein* und *Erweitert*). Alle Tabs halten jetzt rechts einen Abstand ein
- Tab *Erweitert*: „Verbinde neu …“ blieb stehen, obwohl die neue Verbindung längst stand. Jetzt erscheint
  „Verbunden.“, sobald die Verbindung wieder steht, und der Hinweis verschwindet nach ein paar Sekunden (auch bei
  „Test-Presence senden“)
- Das Log wiederholt die Zeile „Language: …“ nicht mehr
- Upload zu catbox.moe: Schlägt er fehl, steht jetzt die Antwort von catbox im Log, damit sich die Ursache finden
  lässt

## 1.5.0

### Neu
- **Tab „Über“**: Autor, Links zu GitHub (Versionen, Problem melden) und der komplette Changelog - die installierte
  Version immer ganz oben
- **Update-Check**: sucht auf GitHub nach einer neuen Version - bei jedem AIMP-Start, einmal am Tag, einmal pro
  Woche oder einmal im Monat (oder abgeschaltet). Neue Versionen werden heruntergeladen, geprüft (SHA-256) und in
  AIMP geöffnet, das sie installiert; eine neue Version wird nur einmal automatisch geöffnet. Buttons „Jetzt
  suchen“ und „Installieren“ im Tab „Über“
- **Live-Vorschau** im Tab *Anzeige*: zeigt, was Discord zeigen wird - die Aktivitätskarte mit Cover, Play-/Pause-
  Symbol, Texten und Fortschrittsbalken sowie deinen Eintrag in der Mitgliederliste - schon beim Tippen, bevor du
  „Übernehmen“ drückst. Läuft nichts, wird ein Beispieltitel gezeigt; ist die Presence ausgeblendet, sagt die
  Vorschau warum
- **Cover-Vorschau** im Tab *Cover*: das aktuelle Cover und woher es kommt (Tags der Datei, Bild im Ordner,
  Deezer, iTunes, ...), mit einem Link zum Bild
- **Tab „Erweitert“**: Verbindungsdetails (Kanal, Application ID, letzte Aktualisierung), „Test-Presence senden“,
  „Neu verbinden“, die letzten Log-Einträge, Presence für bestimmte Playlists ausblenden, Sprache wählen,
  Einstellungen exportieren / importieren. Die Option für die eigene Discord-Anwendung ist hierher umgezogen
- **Sprachen**: Das Plugin folgt der Oberflächensprache von AIMP - Englisch, Deutsch, Russisch und Ukrainisch sind
  eingebaut; die Sprache kann auch im Tab *Erweitert* gewählt werden. Weitere Sprachen lassen sich als
  `Langs\<Name>.lng` ohne neuen Build hinzufügen
- Neuer Platzhalter `%playlist%` (Name der Playlist, aus der der Titel gestartet wurde)

### Geändert
- Der Tab *Links* ist jetzt Teil des Tabs *Anzeige* (6 Tabs: Allgemein, Anzeige, Cover, Quellen, Erweitert, Über)
- `%status%` („Wiedergabe“ / „Pausiert“) erscheint in der Sprache des Plugins
- Windows: Die Einstellungsdatei wird als Unicode (UTF-16) gespeichert - Texte in jeder Sprache (z. B. Kyrillisch
  in eigenen Zeilen oder Filtern) bleiben korrekt erhalten. Alte Dateien werden automatisch umgewandelt

### Behoben
- **Linux**: Das Plugin läuft jetzt auf praktisch jeder Distribution der letzten zehn Jahre (glibc 2.17 oder neuer:
  Ubuntu 18.04+, Debian 9+, Mint, Fedora, Arch, ...). Frühere Builds konnten eine sehr neue glibc brauchen (bis
  2.38, z. B. Ubuntu 24.04) und luden dann nicht
- Linux: Das Plugin gibt seine eingebaute C++-Bibliothek nicht mehr an AIMP weiter (nur noch seinen Einstiegspunkt)
- Das Beenden von AIMP konnte mehrere Sekunden dauern, während online ein Cover gesucht wurde - laufende Downloads
  werden jetzt sofort abgebrochen

### Unter der Haube
- Weniger als die halbe Dateigröße von 1.4.1 (Windows x64: 0,5 MB statt 1,3 MB, Linux: 0,55 MB statt 1,8 MB) und
  seltener CPU-Aufwachen: auf Größe optimiert, keine iostreams- / filesystem-Bibliothek, die Hintergrund-Threads
  wachen nur auf, wenn es etwas zu tun gibt
- Ein gemeinsamer Leser / Schreiber für die Einstellungen unter Windows und Linux; Änderungen von Hand an der
  INI-Datei werden auf beiden Systemen übernommen, während AIMP läuft
- Die Tests decken jetzt auch den Update-Check (Fake-GitHub-Server), die Vorschauen (als Bilder gezeichnet), den
  Sprachwechsel, Export / Import, die Test-Presence, Neu verbinden und den Playlist-Filter ab - unter Linux und
  mit beiden Windows-Builds unter Wine

## 1.4.1

### Behoben
- **Layout der Einstellungsseite**: In 1.4.0 waren die Elemente auf jedem Tab über die Seite verstreut,
  abgeschnitten oder gar nicht sichtbar (Eingabefelder und Auswahllisten). Die Elemente waren nicht verankert,
  deshalb hat AIMP sie verschoben, als der Tab seine endgültige Größe bekam. Alle Elemente sind jetzt oben links
  verankert und behalten ihre Position

### Geändert
- README: neue Screenshots aller Einstellungs-Tabs (im UI-Stil von AIMP)

### Unter der Haube
- Die Tests prüfen jetzt, dass jedes Element der Einstellungsseite verankert ist und eine gültige Größe hat, damit
  dieser Layout-Fehler nicht unbemerkt zurückkommt

## 1.4.0

### Neu
- **Einstellungs-Tab unter Linux**: Die Einstellungsseite wird jetzt mit der UI-API von AIMP (`IAIMPServiceUI`)
  statt mit Windows-Steuerelementen gebaut. Natives AIMP für Linux bekommt dieselbe Seite *Einstellungen ->
  Plugins -> Discord Rich Presence* wie Windows (Allgemein / Anzeige / Cover / Online-Quellen / Links). Danke an
  DarkDrawKill aus dem AIMP-Forum für den Vorschlag
- **Folgt dem AIMP-Skin**: Die Elemente zeichnet AIMP selbst, die Seite passt also zum aktuellen Skin (auch zum
  Dark Mode)
- **Ein Paket für alle Plattformen**: `aimp_discord_rpc.aimppack` enthält Windows 32 Bit, Windows 64 Bit und Linux;
  AIMP wählt den passenden Build

### Geändert
- „Developer Portal“, „App erstellen“ (Spotify) und „Token holen“ (Discogs) sind jetzt Links, die AIMP im Browser
  öffnet (funktioniert auch unter Linux)
- „Cover-Cache leeren“ zeigt die Bestätigung auf der Seite statt in einem Meldungsfenster
- Die INI-Datei unter Linux kann weiterhin von Hand bearbeitet werden, während AIMP läuft

### Behoben
- Einstellungen, die gespeichert wurden, während sich das Plugin (neu) mit Discord verband, wurden erst beim
  nächsten Titelwechsel übernommen

### Unter der Haube
- Kein Win32-UI-Code mehr (keine Abhängigkeit von comctl32 / uxtheme / shell32)
- `tools/make_aimppack.py` baut das Paket; die CI baut und testet alle Plattformen und hängt die `.aimppack` an
- Die Tests bedienen die Einstellungsseite über eine Nachbildung des UI-Dienstes von AIMP (öffnen, bearbeiten,
  Übernehmen, speichern, die Presence nutzt den neuen Wert) unter Linux und mit beiden Windows-Builds unter Wine

## 1.3.0 (im Vergleich zu 1.1.0)

### Neue Plattformen
- **Windows 32 Bit**: neuer x86-Build für 32-Bit-AIMP (der Einstiegspunkt des Plugins wird jetzt auch unter 32 Bit
  richtig exportiert, sodass AIMP es laden kann)
- **Linux (nativ)**: neue `aimp_discord_rpc.so` für AIMP für Linux (x86_64)
  - spricht mit Discord über dessen Unix-Socket; natives Discord, Flatpak, Snap und Vesktop werden automatisch
    gefunden
  - Einstellungen in `~/.config/AIMP/DiscordRPC.ini`, beim ersten Start mit allen Optionen angelegt; Änderungen an
    der Datei werden übernommen, während AIMP läuft
  - Online-Cover nutzen das libcurl des Systems (zur Laufzeit geladen - ohne libcurl funktioniert das Plugin
    trotzdem, nur ohne Online-Cover)
- **Wine**: Die Windows-DLLs erkennen, wenn AIMP unter Wine läuft, und verbinden sich direkt mit dem Linux-Discord -
  kein Bridge-Programm nötig

### Neue Funktionen
- **Anklickbarer Songtitel**: Ein Klick auf den Titel in Discord öffnet eine YouTube-Suche nach Interpret + Titel
  (neuer Tab *Links*; stattdessen kann auch ein eigener Link mit Platzhaltern genutzt werden)
- **Funktioniert ohne eigene Discord-Anwendung**: Das Feld Application ID ist umgezogen nach *Allgemein ->
  Erweitert: eigene Discord-Anwendung verwenden*. Standardmäßig wird die eingebaute Anwendung genutzt
- **PreMiD-freundlich**: Während der Pause wird die Presence standardmäßig ausgeblendet, damit andere Aktivitäten
  (z. B. PreMiD) angezeigt werden. „Pausiert-Status anzeigen“ gibt es weiterhin

### Geändert
- Die zweite Zeile ist jetzt standardmäßig `by %artist%` (vorher `%artist%`)
- Die Pause-Option „Presence löschen“ heißt jetzt „Ausblenden (PreMiD u. a. sichtbar)“
- Der Tab *Buttons* wurde entfernt (Discord hat die Buttons im eigenen Profil nie angezeigt) - ersetzt durch den
  anklickbaren Titel
- Die Felder für Asset-Schlüssel (Play- / Pause- / Ersatz-Symbol) wurden entfernt; es werden die Symbole der
  eingebauten Anwendung genutzt
- Einstellungen aus 1.1.0 werden automatisch übernommen: Eine eigene Application ID bleibt erhalten und „eigene
  Discord-Anwendung verwenden“ wird eingeschaltet. Das Verhalten bei Pause wird beim Update einmalig auf
  „Ausblenden“ gesetzt (kann im Tab *Allgemein* zurückgestellt werden)
