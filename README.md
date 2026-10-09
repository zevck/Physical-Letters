# Physical Letters
A companion mod for [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) that lets you write letters to NPCs and have them delivered. The recipient reads your letter, remembers it, and replies. NPCs also write to you, and to each other, on their own.

## ✉️ Features
### Writing Letters
Write a letter by hand on a sheet of parchment. Every letter starts with a "To:" heading and requires the recipient's name and address (e.g. Lydia, 7890 Whiterun). 

The address is comprised of the last 4 digits of the NPCs SkyrimNet UUID and home location. While writing, matching NPC's will autocomplete. If there are multiple NPCs sharing a name, pressing Tab will cycle through them.

This feature requires **Ink & Quill**.

**Crafting**
Craft **3** parchment from **1** roll of paper at a tanning rack, or buy it from general goods merchants.

### Mailing Letters
Give a letter to an innkeeper or courier to mail it (20 gold default). Delivery time follows the same speed as fast travel and can be configured in the MCM. A letter to someone who has died or can't be found is returned to sender.

Alternatively, you can hand an NPC the letter in person.

### Replies
The recipient reads your letter through SkyrimNet, remembers it, and may write back. NPCs remember your earlier letters, so a correspondence builds over time. Their reply is delivered by the courier.

### The Courier
When a letter is scheduled to be delivered to an NPC and you are nearby, the courier will approach the NPC and deliver the letter in person. Distant deliveries are handled off-screen.

While traveling the roads of Skyrim, you may encounter the courier out on a delivery. He carries real letters that can be intercepted and read. Pick his pocket, shake him down, or hand him a letter you need delivered.

### NPCs Write to You
About once a week, someone you know may write to you if they have a reason to: news, gratitude, worry, a favor to ask, or simply missing you. Former followers, your spouse and your children grow more likely to write the longer you've been apart.

### Letters Between NPCs
NPCs write to each other too, and their letters are delivered by the courier. Intercept one and you can read it, or hand it to its recipient yourself.

### Written in Blood
With no ink, **Ink & Quill** lets you write in your own blood. The recipient sees which parts are written in blood, and reacts to it.

## 📋 Requirements
> [!NOTE]
> Physical Letters supports all versions of Skyrim. VR and 1.7.104 are currently untested.
- [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) Beta 26+
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) (for MCM)
- [SKSE](https://skse.silverlock.org/)
- [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) or [VR Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
- [Ink & Quill](https://github.com/zevck/Ink-and-Quill) to write your own letters

## 🌐 Localization
The mod supports all 9 official Skyrim languages: English, French, German, Italian, Spanish, Polish, Russian, Traditional Chinese, and Japanese. Letter names, item descriptions, the "To:" line, parchment and MCM menus are all localized automatically based on your game language.

### Language Override
If your game language is set to English but you want letters in another language, add a `Language` line to `PhysicalLetters.ini` under `[General]`:

```ini
[General]
Language = GERMAN
DebugLog = 0
```

This will load `Locales/GERMAN.ini` for letter text. The value must match the name of a locale file in the `Locales` folder. Ensure you have the proper fonts installed to support that language.

### Adding a New Language
Community translators can add support for any language without recompiling the plugin. Two files are needed:

**1. Locale file** - `SKSE/Plugins/PhysicalLetters/Locales/{LANGUAGE}.ini`

This controls the text written on and about letters. Example:

```ini
; SKSE/Plugins/PhysicalLetters/Locales/PORTUGUESE.ini

[Letters]
To = Para:
Letter = Carta
LetterTo = Carta para {Name}
LetterFrom = Carta de {Name}
Card = Uma carta para {Recipient} de {Author}.
ReturnDeceased = Devolver ao remetente (falecimento)
ReturnNotFound = Devolver ao remetente (paradeiro desconhecido)
Unreadable = A tinta escorreu; a carta não pode ser lida.
Parchment = Pergaminho
```

Available placeholders:
- `{Name}` - the recipient (in LetterTo) or the author (in LetterFrom)
- `{Recipient}` - who the letter is for (in Card)
- `{Author}` - who wrote it (in Card)
> [!NOTE]
> Names are filled in as they are, so write each line to read correctly for any name: avoid words that change with the person's gender or with the name's first letter. A space follows the "To:" label unless it ends in a full-width colon. Any key left out falls back to English.

**2. MCM translation file** (optional) - `Interface/Translations/Physical Letters_{LANGUAGE}.txt`

This translates the in-game settings menu. Use the English file as a template. If you would like to correct or contribute any translations feel free to submit a PR.

## 🗺️ Planned Features
**Forgeries** - send an NPC a letter from another NPC. NPCs have a percent chance to see through the deception. Potential integration with the vanilla quest's Quill of Gemination for a higher chance.

**Courier Jobs** - work as a courier to deliver real letters across Skyrim that shape the world.

**Messenger Birds** - a perk to mail letters from anywhere in the world with a power.

## 🗒️ Notes
- Every letter an NPC reads or writes is an LLM call through SkyrimNet. How often NPCs write to you and to each other can be changed or turned off in the MCM. Higher letter counts make the world more lively, but also increase costs.
- On default settings, each letter dispatch is around $0.0018, though it depends on your configured LLMs. This runs about once an in game week, so costs are quite low.
- Letter replies are roughly the same as dialogue output, maybe a little higher depending on letter length.
- Physical Letters currently uses SkyrimNet's meta variant for selection, and default/dialogue for writing.
- By default you can only write to unique NPCs. The MCM can also allow generic NPCs SkyrimNet has memories for, or any named NPC.

## 🔑 License
Physical Letters is released under the GNU General Public License v3.0 or later (GPL-3.0-or-later). See [LICENSE.md](LICENSE.md) for the full text.
