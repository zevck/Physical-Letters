/*
 * SPDX-License-Identifier: MIT
 *
 * Physical Letters - a Skyrim SKSE plugin for writing letters to NPCs, having them delivered, and receiving their
 * replies through SkyrimNet: its C API for other SKSE plugins (docs/API.md).
 * Copyright (C) 2026 Zevick
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 * to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of
 * the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * For client mods: copy this header into your project.  It is plain C, so any compiler and CRT work.
 */

/*
 * Getting it, from kDataLoaded or later, on the game thread:
 *
 *     #include "PhysicalLettersAPI.h"
 *
 *     static const PL_API* g_pl = nullptr;
 *
 *     void OnDataLoaded()
 *     {
 *         auto* module = GetModuleHandleA("PhysicalLetters.dll");
 *         auto get = module ? reinterpret_cast<PL_GetAPI_t>(GetProcAddress(module, "PL_GetAPI")) : nullptr;
 *         g_pl = get ? get(PL_API_VERSION) : nullptr;  // NULL: Physical Letters is missing or older than this header
 *     }
 *
 * The contract (docs/API.md has the rest):
 *
 *  - Threads: call every function from the game thread yourself (an SKSE message, a Papyrus native, a task you
 *    queued with SKSE::GetTaskInterface()->AddTask).  Nothing is marshalled for you: from another thread they race
 *    the game.  Every callback is called on the game thread, in a task of its own, never inside Physical Letters'
 *    own work, so a callback may call any of these functions, SendLetter included.
 *  - Your input: every string and struct you pass is copied before the function returns; free it whenever you
 *    like.  `user` pointers are stored as they are and handed back to your callbacks.
 *  - Our output: callback arguments (letterId, values) are valid only during the callback; copy what you keep.
 *    GetLetter's strings stay valid until the next GetLetter call, QueryLetters' until the next QueryLetters call.
 *  - Versions: see PL_GetAPI_t at the end.
 *
 * The examples below assume g_pl is set.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PL_API_VERSION 1  /* docs/API.md#versions */
#define PL_ID_SIZE 33     /* a letter id: 32 hex digits and the terminator */

#ifdef __cplusplus
extern "C" {
#endif

/* A letter to send (docs/API.md#sending): what the DM Letter's page sends. */
typedef struct PL_Send
{
    uint32_t size;        /* sizeof(PL_Send) */
    uint32_t writer;      /* the writer's FormID; 0: nobody writes it (unsigned, or signed with `signature`) */
    const char* signature;/* with writer 0: the name the letter bears, or NULL / "" for an unsigned letter */
    uint32_t recipient;   /* the recipient's FormID; 0x14 is the player */
    const char* text;     /* the letter, or notes with `transform`; NULL / "" with a writer: they choose the subject */
    bool transform;       /* the writer puts the text in their own words (one LLM call); never for a forgery */
    bool forgery;         /* in the writer's name, by nobody they know: it always passes, nobody remembers writing it */
    int32_t hours;        /* game hours until delivery: -1 the travel time, 0 at once, more that many (see SendLetter) */
    const char* tag;      /* NULL / "" or your label, at most 64 bytes: QueryLetters finds it; replies inherit it */
} PL_Send;

/* A letter, as GetLetter fills it: the strings are valid until the next GetLetter call. */
typedef struct PL_Letter
{
    uint32_t size;           /* sizeof(PL_Letter): set it before the call; newer versions only add at the end */
    uint32_t author;         /* FormID; 0 when nobody wrote it (unsigned, signed, a forgery) */
    uint32_t recipient;      /* FormID; 0x14 the player */
    const char* senderName;  /* whom it says it's from: the author, the signature, the forged name; "" unsigned */
    const char* text;
    const char* reply;       /* the recipient's reply, once they've read it; "" otherwise */
} PL_Letter;

/* One kind of item in a letter's package (docs/API.md#packages).  It never grows: GetPackage fills an array of it,
   so anything more about an item comes through a new function. */
typedef struct PL_Item
{
    uint32_t form;   /* the item's FormID */
    int32_t count;
} PL_Item;

/* What happened to a letter (docs/API.md#events).  `actor`: the recipient, the reader, or 0. */
typedef enum PL_Event
{
    PL_EVENT_DELIVERED = 1,  /* it's in the recipient's inventory (the player's: the courier handed it over) */
    PL_EVENT_READ = 2,       /* an NPC read it; `replied` says whether they wrote back */
    PL_EVENT_FAILED = 3,     /* a letter that was to be written never was (the call failed or timed out) */
} PL_Event;
/* `letterId` is valid only during the call. */
typedef void (*PL_EventCallback)(void* user, int32_t event, const char* letterId, uint32_t actor, bool replied);

/* A letter action's parameter (docs/API.md#actions): its name, and what the model is asked to put in it. */
typedef struct PL_Param
{
    const char* name;
    const char* description;
} PL_Param;
/* The reader of `letterId` chose the action: `values` are its parameters' values, in the order registered, always
   strings as the model wrote them ("" for one it left out), even for a parameter described as a number: an hour
   comes as "20" or "8 in the evening", and parsing it is yours.  `letterId` and `values` are valid only during the
   call. */
typedef void (*PL_ActionCallback)(void* user, const char* letterId, uint32_t reader, const char* const* values, int32_t count);

/* Which letters' readers are offered an action: the player's letters, NPCs' (every other: the schedule's, the
   DM Letter's, the API's), or both. */
typedef enum PL_Letters
{
    PL_LETTERS_ALL = 0,
    PL_LETTERS_PLAYER = 1,
    PL_LETTERS_NPC = 2,
} PL_Letters;

/* A letter action (docs/API.md#actions). */
typedef struct PL_Action
{
    uint32_t size;                   /* sizeof(PL_Action) */
    const char* name;                /* letters, digits and _, at most 40: what the model names, and the event's */
    const char* displayName;         /* the MCM's label for it, at most 60 bytes */
    const char* displayDescription;  /* the MCM's tooltip: what it does, for the player */
    const char* description;         /* for the model: when a reader would choose it */
    const PL_Param* params;
    int32_t count;                   /* params, at most 8 */
    int32_t letters;                 /* PL_Letters */
    PL_ActionCallback callback;      /* may be NULL: the mod event alone */
    void* user;
} PL_Action;

typedef struct PL_API
{
    uint32_t size;     /* sizeof(PL_API) in the DLL: newer versions only add at the end */
    uint32_t version;  /* the DLL's PL_API_VERSION */

    /* Sends the letter; idOut (PL_ID_SIZE) gets its id, which the events name.  False: refused, with the reason
       in PhysicalLetters.log.  A transformed letter (or one with a writer and no text) is written by one LLM call
       after this returns: its id is valid at once (GetLetter fails until it's written, AddToPackage works), and
       PL_EVENT_FAILED names it if it's never written.

       `hours`, counted from when the letter exists (a transformed one: once written):
         -1  the travel time between the writer and the recipient (with no writer, a fixed fallback).
          0  at once: the player finds it in their inventory; an NPC gets it about 30 real seconds later (held so
             you can add to its package).
         >0  that many game hours.
       With -1 or more than 0, a letter to the player goes to the vanilla courier when due, who brings it when he
       finds them.  A letter to an NPC goes into their inventory when due, wherever they are, with no courier.

           // Lydia (0x000A2C94) writes to the player, in her own words, from these notes; it comes by courier.
           PL_Send send{};
           send.size = sizeof(PL_Send);
           send.writer = 0x000A2C94;
           send.recipient = 0x14;
           send.text = "She heard bandits were seen near Riverwood and wants the Thane to be careful.";
           send.transform = true;
           send.hours = -1;
           send.tag = "mymod_warning";
           char id[PL_ID_SIZE];
           if (g_pl->SendLetter(&send, id)) SKSE::log::info("Sent letter {}", id);
    */
    bool (*SendLetter)(const PL_Send* send, char* idOut);

    /* The letter, if Physical Letters knows it.

           PL_Letter letter{};
           letter.size = sizeof(PL_Letter);
           if (g_pl->GetLetter(id, &letter)) {
               SKSE::log::info("From {}: {}", letter.senderName, letter.text);
               if (*letter.reply) SKSE::log::info("They replied: {}", letter.reply);
           }
    */
    bool (*GetLetter)(const char* letterId, PL_Letter* out);

    /* Every letter's events, whoever sent it.  Returns the listener's id (0: failed); remove it before unloading.

           static void OnLetterEvent(void* user, int32_t event, const char* letterId, uint32_t actor, bool replied)
           {
               if (event == PL_EVENT_READ && replied) SKSE::log::info("0x{:X} answered letter {}", actor, letterId);
           }
           uint64_t listener = g_pl->AddListener(OnLetterEvent, nullptr);
    */
    uint64_t (*AddListener)(PL_EventCallback callback, void* user);

    /* Stops a listener's events.

           g_pl->RemoveListener(listener);
    */
    void (*RemoveListener)(uint64_t listener);

    /* An action a letter's reader may choose, listed in readings while registered (docs/API.md#actions).
       Register at kDataLoaded or later; the same name again replaces it.  False: a bad name or too many.

           static void OnMeeting(void* user, const char* letterId, uint32_t reader, const char* const* values, int32_t count)
           {
               PL_Letter letter{};
               letter.size = sizeof(PL_Letter);
               if (!g_pl->GetLetter(letterId, &letter) || count < 2) return;
               // values[0] the place, values[1] the hour: bring reader and letter.author there then.
           }
           static const PL_Param params[] = {
               { "place", "where they will meet, as the letter names it" },
               { "hour", "the hour of the day they will meet, 0 to 23" },
           };
           PL_Action action{};
           action.size = sizeof(PL_Action);
           action.name = "meeting";
           action.displayName = "Meetings";
           action.displayDescription = "People who agree by letter to meet go there at the hour.";
           action.description = "The letter asks to meet, and the reader agrees to come.";
           action.params = params;
           action.count = 2;
           action.letters = PL_LETTERS_ALL;
           action.callback = OnMeeting;
           g_pl->RegisterAction(&action);
    */
    bool (*RegisterAction)(const PL_Action* action);

    /* Takes an action out of the readings.

           g_pl->UnregisterAction("meeting");
    */
    void (*UnregisterAction)(const char* name);

    /* The reply to `letterId` (idOut, PL_ID_SIZE): false if it has none yet.  In an action's callback the reply,
       if the reader wrote one, exists already.

           char replyId[PL_ID_SIZE];
           if (g_pl->GetReplyId(letterId, replyId)) SKSE::log::info("Letter {} was answered with {}", letterId, replyId);
    */
    bool (*GetReplyId)(const char* letterId, char* idOut);

    /* Adds `count` of item `form` to the letter's package while it's still on its way and in no one's hands:
       while a transformed letter is being written, then until it's delivered into an NPC's inventory (when due,
       and never within 30 real seconds of SendLetter or of a reply being written) or a courier takes it (the
       vanilla courier for a letter to the player; for some replies, the courier in town or on the road).  So:
       right after SendLetter, or in an action's callback for the reply.  Not a letter sent to the player with
       `hours` 0: it's in their inventory when SendLetter returns (a transformed one, once written: until then it
       can be added to).  False otherwise; PhysicalLetters.log says why.  The items are created: nothing is taken from anyone, so for
       a payment out of an NPC's purse, remove it from them yourself first.  Any inventory item but a leveled list.
       If the letter never arrives (never written, its recipient dead), what you added is deleted, not given to the
       player.

           // In a "repay_loan" action's callback: 500 gold (0x0000000F) travels with the reader's reply.
           char replyId[PL_ID_SIZE];
           if (g_pl->GetReplyId(letterId, replyId)) g_pl->AddToPackage(replyId, 0x0000000F, 500);
    */
    bool (*AddToPackage)(const char* letterId, uint32_t form, int32_t count);

    /* The letter's package: what it holds, or what arrived once delivered.  Fills up to `max` items and returns
       how many kinds there are (0: no package); `out` may be NULL with `max` 0 to count.

           PL_Item items[16];
           const int32_t kinds = g_pl->GetPackage(letterId, items, 16);
           for (int32_t i = 0; i < kinds && i < 16; ++i) {
               if (items[i].form == 0x0000000F) SKSE::log::info("{} gold came with it", items[i].count);
           }
    */
    int32_t (*GetPackage)(const char* letterId, PL_Item* out, int32_t max);

    /* The letters this save has, as a JSON array (docs/API.md#finding-letters): valid until the next call.
       `actor` 0: anyone's.  `filterJson` NULL / "{}": the newest 50.  Keys: maxCount, role ("either", "writer",
       "recipient"), other (a FormID), includeTags, excludeTags, writtenAfter, writtenBefore (game days),
       orderBy ("WrittenDesc", "WrittenAsc").  "[]" for none or a bad filter (logged).  Each letter: id, author,
       recipient, sender_name, text, written_at, in_reply_to, tag, package ([{ form, count }]).

           // The ten newest letters the player received with this mod's tag (replies to its letters carry it).
           const char* json = g_pl->QueryLetters(0x14, R"({"role":"recipient","includeTags":["mymod_warning"],"maxCount":10})");
           for (const auto& letter : nlohmann::json::parse(json)) {
               SKSE::log::info("{}: {}", letter["sender_name"].get<std::string>(), letter["text"].get<std::string>());
           }
    */
    const char* (*QueryLetters)(uint32_t actor, const char* filterJson);
} PL_API;

/* Exported by PhysicalLetters.dll as "PL_GetAPI": the example at the top of this file.  Pass the PL_API_VERSION of
   the header you built with.  It returns NULL if the DLL is older than that, else the DLL's own table, whatever
   version you asked for: newer versions only add functions at the end, so a table from a newer DLL works with an
   older header as it is.  To use a function a later version added, check `version` (or that `size` reaches it)
   first. */
typedef const PL_API* (*PL_GetAPI_t)(uint32_t version);

#ifdef __cplusplus
}
#endif
