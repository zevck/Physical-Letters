# Physical Letters
A companion mod for [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) that lets you write letters to NPCs and have them delivered. The recipient reads your letter, remembers it, and replies. NPCs also write to you, and to each other, on their own.

## ✉️ Features
### Writing Letters
Write a letter by hand by using parchment. Parchment can be purchased from a general goods merchant or crafted from rolls of paper at a tanning rack. Every letter starts with a "From:" heading, prefilled with your character's name, and a "To:" heading that requires the recipient's name and address (e.g. Lydia, 7890 Whiterun). You may replace your name on the "From:" line with anything to remain anonymous. When you are done writing, press **F3** (default) to save.

The address is comprised of the last 4 digits of the NPC's SkyrimNet UUID and home location. While writing, matching NPCs will show a preview of their address so you don't actually need to know it. Pressing **Right Arrow** will autocomplete. If there are multiple NPCs sharing a name, pressing **Tab** will cycle through them.

This feature requires [Ink & Quill](https://github.com/zevck/Ink-and-Quill). By default, writing requires a quill and inkwell in your inventory. Alternatively, **Ink & Quill** allows you to write in your own blood if you don't have any ink, which the reader will notice.

### Mailing Letters
Give a letter to an innkeeper or courier to mail it (20 gold default). Delivery time follows the same speed as the time that passes from fast travel and can be configured in the MCM. A letter to someone who has died or can't be found is returned to sender. Alternatively, you can hand an NPC the letter in person. When mailing a letter you have the option to attach a package and add items to be delivered to the recipient.

When a letter is scheduled to be delivered to an NPC and you are nearby, the courier will approach the NPC and deliver the letter in person. Distant deliveries are handled off-screen. While traveling the roads of Skyrim, you may encounter the courier out on a delivery. He carries real letters that can be intercepted and read. Pick his pocket, shake him down, or hand him a letter you need delivered.

### NPCs Write
NPCs you write to read your letters through SkyrimNet, remember them, and may write back. NPCs remember your earlier letters, so a correspondence builds over time. Their reply is delivered by the courier. About once a week, someone you know may write to you if they have a reason to: news, gratitude, worry, a favor to ask, or simply missing you. Former followers, your spouse and your children grow more likely to write the longer you've been apart.

NPCs write to each other as well, and their letters are delivered by the courier. Intercept one and you can read it, or hand it to its recipient yourself. The frequency at which NPCs write can be configured in the MCM.

### Forgeries
You can forge letters by replacing your name on the "From:" line with someone else's. Frame someone you don't like, impersonate someone to learn secrets, or just sow chaos. NPCs have a chance to detect that the handwriting does not match the alleged writer, based on your pickpocket skill. They won't know who sent it.

**Ink & Quill** allows you to use the Quill of Gemination, an enchanted quill that requires special ink which can perfectly replicate any handwriting. Forgeries written using this special ink and quill are undetectable. The Quill of Gemination can be obtained during the quest Vald's Debt as part of the Thieves Guild quest-line.

### DM Letter
The DM Letter is an item that can be spawned from the MCM to dispatch legitimate letters between any actors. This can be used to seed stories in your world or just experiment with Physical Letters. NPCs that you write letters for on their behalf remember them as if they wrote them themselves. It includes options to assign the sender and recipient, the delivery time (in hours), and whether its a forgery or to include a package. Leave the contents of the letter blank to have the LLM decide on a subject, write the letter yourself, or check Transform to have the LLM convert your prompt into a full letter.

## 📋 Requirements
> [!NOTE]
> Physical Letters supports all versions of Skyrim.
- [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) Beta 29.1+
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) for the MCM
- [SKSE](https://skse.silverlock.org/)
- [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) or [VR Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
- [Ink & Quill](https://github.com/zevck/Ink-and-Quill) v0.2 or later to write your own letters

## 🌐 Localization
The mod supports all 9 official Skyrim languages: English, French, German, Italian, Spanish, Polish, Russian, Traditional Chinese, and Japanese. Letter names, item descriptions, the "From:" and "To:" lines, parchment and MCM menus are all localized automatically based on your game language.

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
From = De:
Letter = Carta
LetterTo = Carta para {Name}
LetterFrom = Carta de {Name}
Card = Uma carta para {Recipient} de {Author}.
CardTo = Uma carta para {Recipient}.
ReturnDeceased = Devolver ao remetente (falecimento)
ReturnNotFound = Devolver ao remetente (paradeiro desconhecido)
Unreadable = A tinta escorreu; a carta não pode ser lida.
Parchment = Pergaminho
```

Available placeholders:
- `{Name}` - the recipient (in LetterTo) or the author (in LetterFrom)
- `{Recipient}` - who the letter is for (in Card)
- `{Author}` - the name it's signed with (in Card); CardTo is the card of an unsigned letter
> [!NOTE]
> Names are filled in as they are, so write each line to read correctly for any name: avoid words that change with the person's gender or with the name's first letter. A space follows the "From:" and "To:" labels unless they end in a full-width colon. Any key left out falls back to English.

**2. MCM translation file** (optional) - `Interface/Translations/Physical Letters_{LANGUAGE}.txt`

This translates the in-game settings menu. Use the English file as a template. If you would like to correct or contribute any translations feel free to submit a PR.

## 🗺️ Planned Features
**Courier Jobs** - work as a courier to deliver real letters across Skyrim that shape the world.

**Messenger Birds** - a perk to mail letters from anywhere in the world with a power.

**Better VR Support** - a way for VR users to compose a letter without a keyboard.

## 🗒️ Notes
- Every letter an NPC reads or writes is an LLM call through SkyrimNet. How often NPCs write to you and to each other can be changed or turned off in the MCM. Higher letter counts make the world more lively, but also increase costs.
- On default settings, each letter dispatch is around $0.0018, though it depends on your configured LLMs. This runs about once an in game week, so costs are quite low.
- Letter replies are roughly the same as dialogue output, maybe a little higher depending on letter length.
- Physical Letters currently uses SkyrimNet's meta variant for selection, and default/dialogue for writing.
- By default you can only write to unique NPCs. The MCM can also allow generic NPCs SkyrimNet has memories for, or any named NPC.

## 🔑 License
Physical Letters is released under the GNU General Public License v3.0 or later (GPL-3.0-or-later). See [LICENSE](LICENSE) for the full text.

### Client API Exception
As an additional permission under GPLv3 section 7, you may copy, include, compile against, dynamically load, link to, and communicate with Physical Letters through its public APIs, the C API declared in `api/PhysicalLettersAPI.h` and the Papyrus script `Source/Scripts/PhysicalLetters.psc`, and you may distribute such client mods under terms of your choice.
