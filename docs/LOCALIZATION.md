# Localization

Two kinds of text, in two places:

- **The MCM** (labels, tooltips): `$PL_…` keys in `Interface/Translations/Physical Letters_<LANGUAGE>.txt`, which the game picks by its own language ([SETTINGS.md](SETTINGS.md#the-mcm)).
- **Letter text**: what's written into letter items. Locale files, `SKSE/Plugins/PhysicalLetters/Locales/<LANGUAGE>.ini`, as SkyrimNet Physical Diaries' (SNPD `docs/LOCALIZATION.md`). Code: `src/Locale.cpp`, read through `include/Strings.h`.

## Which language

1. `[General] Language` in `PhysicalLetters.ini`, if set: lets an English game write, say, German letters. INI only, no MCM option (as SNPD's). `Config::Save` keeps it, first in `[General]`.
2. Otherwise the game's `sLanguage`, read from the game's own settings (`INISettingCollection`, `sLanguage:General`), not from `Skyrim.ini` on disk as SNPD does: that file's folder differs for VR and GOG.

The name is upper-cased and picks `<LANGUAGE>.ini`: ENGLISH, CHINESE, FRENCH, GERMAN, ITALIAN, JAPANESE, POLISH, RUSSIAN or SPANISH ship. No file for the language, or a key it leaves out, gives the English text. The log says which file was loaded and how many texts it held.

Loaded once at `kDataLoaded`, before anything uses it (the parchment is named then), and only read after, from any thread.

## The keys

All in `[Letters]`, UTF-8. A line starting with `;` is a comment; a `;` later in a line is text.

| Key | English | Where |
|---|---|---|
| `To` | `To:` | The player's letter's first line, before the recipient ([WRITING.md](WRITING.md#the-text)). A space follows it, unless it ends in a full-width colon (Chinese "收件人：", Japanese "宛先：") |
| `Letter` | `Letter` | A letter's item name when the name of who it's to or from is unknown |
| `LetterTo` | `Letter to {Name}` | The item name of the player's letters and letters between NPCs; `{Name}` is the recipient |
| `LetterFrom` | `Letter from {Name}` | The item name of letters to the player; `{Name}` is the author |
| `Card` | `A letter to {Recipient} from {Author}.` | The item card under the model in the inventory |
| `ReturnDeceased`, `ReturnNotFound` | `Return to sender (deceased)`, `(not found)` | Added to a returned letter's item card ([DELIVERY.md](DELIVERY.md#undeliverable-letters)) |
| `Unreadable` | `The ink has run; the letter can't be read.` | The page of a letter LetterDB has no text for |
| `Parchment` | `Parchment` | The blank letter's item name; the ESP's is English, so it's set at `kDataLoaded` |

Placeholders let a language put the name where it needs it ("{Name}への手紙"). The code knows nothing about a name: not its gender, its case or its first letter. So a text has to read right for **any** name, a man's or a woman's, starting with a vowel or a consonant:

- **No word may change with the person's gender.** "destinataire décédé", "Empfänger", "destinatario fallecido", "адресат умер" and "adresat zmarł" are all wrong for a woman. Use a noun or a form that doesn't change: "(décès)", "(verstorben)", "(decesso)", "(por fallecimiento)", "(адресата нет в живых)", "(adresat nie żyje)".
- **No word may change with the name's first sound.** French "de"/"à" before a name need elision before a vowel ("Lettre de Aela" should be "Lettre d'Aela"), so French uses "Lettre pour {Name}" and "Lettre signée {Name}" ("signée" agrees with "lettre", never with the person).
- **Names aren't declined.** Polish and Russian use label forms ("List (do: {Name})", "Письмо (кому: {Name})"), so a name in the nominative still reads right.

A letter is always one letter, so number never varies. The French "To:" is "Destinataire :", which reads as a label where a literal "À :" doesn't (user, 2026-10-04). On 2026-10-04 another agent "corrected" six files back into gendered and elided forms; they were reverted for these reasons.

## Letters already written

Nothing language-dependent is stored for the text: the "To:" label, the card and the return note are built every time. Item names are kept in the co-save (`DynamicForms`), so `Letters::AttachTexts` names each letter again in the current language once LetterDB is open, and the save keeps the new name. Until then (moments after a load) a letter shows the name it was saved with.

## Not localized yet

The messages shown when a letter can't be saved (`Strings::Write…`, `kWriteNotReady`) are English. So is what goes into prompts: SkyrimNet's prompts are English, and so are the sentences Physical Letters adds to them (`Letters::BloodSentence`).

The eight non-English files were written by Claude (2026-10-04); they ship until native speakers correct them.
