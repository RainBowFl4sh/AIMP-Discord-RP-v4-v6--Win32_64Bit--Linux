# Security policy

## Supported versions

Only the **latest release** gets fixes. The plugin's update check (*About* tab) installs it for you.

| Version | Supported |
|---|---|
| latest release | yes |
| older versions | no - please update first |

## Reporting a vulnerability

Please **do not open a public issue** for security problems.

Report them privately through GitHub: **Security -> Advisories -> Report a vulnerability** in this repository.
Describe what is affected, how to reproduce it and, if you can, what could be done with it.

You will get an answer as soon as possible - this is a hobby project, so please allow a few days. Once the problem
is fixed and a new version is released, the report can be published, with credit to you if you want.

## What is in scope

- the plugin itself (`aimp_discord_rpc.dll` / `.so`) and its update check / update package handling
- the cover upload (what is sent where) and the local settings / cache files

Not in scope: AIMP, Discord, the AIMP SDK and the upload / lookup services the plugin uses (x0.at, catbox.moe,
Imgur, Deezer, ...) - please report problems with those to their makers.
