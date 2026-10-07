# Localization (i18n)

FakeBots' own runtime text (used by `FakeBotGetText`) is fully
data-driven: it lives in plain JSON files under `plugins/FakeBots/lang/`,
never hard-coded in the compiled plugin. The plugin itself defines zero
admin commands or menus - the language system exists purely so *your*
gamemode/filterscript code has ready-made, swappable strings to build
admin tooling, chat feedback, logs, etc. on top of.

## File layout

| File | Language |
|---|---|
| `FakeBots.json` | English - **always loaded as the fallback**, regardless of active language |
| `FakeBots.fa.json` | Persian (فارسی) |
| `FakeBots.ru.json` | Russian (Русский) |

Add your own by dropping `FakeBots.<code>.json` in the same folder - no
recompilation needed.

## Switching language

```pawn
FakeBotSetLanguage("fa"); // loads FakeBots.fa.json
FakeBotSetLanguage("ru"); // loads FakeBots.ru.json
FakeBotSetLanguage("en"); // loads FakeBots.json (also the default)
```

`FakeBotSetLanguage()` returns `false` if the requested file couldn't be
found/parsed - in that case FakeBots automatically keeps using the
default English text so `FakeBotGetText()` never starts returning empty
strings.

## Looking up text

```pawn
new msg[128];
FakeBotGetText("bot_created", msg, sizeof(msg));
```

Resolution order for a given key:

1. The active language file.
2. The default `FakeBots.json` (English), if the key is missing from the
   active language.
3. The key itself, if it's missing from both - so a typo or an
   untranslated string is always visibly obvious in-game rather than
   silently blank.

## Formatting

FakeBots' text strings use classic `%s`/`%d`/`%.2f`-style placeholders
you fill in with Pawn's `format()`, exactly like any other string:

```pawn
new template[128], line[128];
FakeBotGetText("bot_list_entry", template, sizeof(template));
format(line, sizeof(line), template, botid, name, stateText);
SendClientMessage(playerid, -1, line);
```

## Changing the language directory

```pawn
FakeBotSetLanguageDirectory("filterscripts/FakeBots/lang");
FakeBotSetLanguage("fa");
```

Call `FakeBotSetLanguageDirectory()` **before** `FakeBotSetLanguage()` if
you keep your language files somewhere other than the default
`plugins/FakeBots/lang`.

## Hot-reloading

```pawn
FakeBotReloadLanguage();
```

Re-reads the currently active language file from disk without restarting
the server - handy while iterating on translations.

## Default key reference

See `lang/FakeBots.json` for the full list of keys FakeBots ships with
out of the box (bot lifecycle, movement, driving, generic command
messages). These are suggestions, not a fixed schema - add whatever keys
your own gamemode needs to any of the language files and read them back
with `FakeBotGetText()`.
