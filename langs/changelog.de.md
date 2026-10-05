# Änderungen

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
