# Discord Bridge for open.mp and SA-MP

> Versão em português: [README.pt-BR.md](README.pt-BR.md)

Connect your open.mp or SA-MP server to Discord and control everything from
Pawn: `/` commands, buttons, menus, forms, rich messages, moderation and much
more.

```pawn
public OnGameModeInit()
{
    DBR_CreateCommand("ping", "Replies with pong", .callback = "Cmd_Ping");
}

forward Cmd_Ping(DiscordInteraction:interaction, DiscordUser:user);
public Cmd_Ping(DiscordInteraction:interaction, DiscordUser:user)
{
    return DBR_RespondInteraction(interaction, "Pong!");
}
```

## Contents

- [Features](#features)
- [Installation](#installation)
  - [1. Install the plugin](#1-install-the-plugin)
  - [2. Create the bot on Discord](#2-create-the-bot-on-discord)
  - [3. Configure the token](#3-configure-the-token)
  - [Choosing intents](#choosing-intents)
- [Concepts in 2 minutes](#concepts-in-2-minutes)
- [Quick guide](#quick-guide)
- [Callbacks](#callbacks)
- [Troubleshooting](#troubleshooting)
- [Building the plugin](#building-the-plugin)

## Features

| Feature | What you can do | Example |
| --- | --- | --- |
| Messages | Send, reply, edit, delete, pin, react, clear a channel, DM | [12](examples/en/12-messages.pwn) |
| Embeds | Title, colour, author, fields, images, footer | [02](examples/en/02-embeds.pwn) |
| Slash commands | Options, choices, subcommands, autocomplete, permissions, context menus | [03](examples/en/03-slash-commands.pwn) |
| Buttons | Every style, prefix handlers, updating the message | [04](examples/en/04-buttons.pwn) |
| Select menus | Your own options, users, roles and channels | [05](examples/en/05-select-menus.pwn) |
| Modals | Forms with text fields, menus and file uploads | [06](examples/en/06-modals.pwn) |
| Components V2 | Messages built from containers, sections, galleries and separators | [07](examples/en/07-components-v2.pwn) |
| Interactions | Private and deferred responses, follow-ups, editing, global filter | [08](examples/en/08-interaction-responses.pwn) |
| Moderation | Kick, ban, unban, timeout, nickname, roles, voice | [09](examples/en/09-moderation.pwn) |
| Lookups | Servers, members, users, roles and channels | [10](examples/en/10-lookups.pwn) |
| Bot profile | Status, activity, name, avatar, banner, description, nickname | [11](examples/en/11-bot-profile.pwn) |

All network work runs in the background: the plugin never blocks the server.

Complete examples: [in English](examples/en/README.md) · [em português](examples/pt-BR/README.md).

## Installation

### 1. Install the plugin

Download the package from the [releases page](https://github.com/itsneufox/omp-Discord-Bridge/releases)
and copy the files:

| Server | Where to copy |
| --- | --- |
| open.mp | contents of `components/` into the server's `components/` folder |
| SA-MP | contents of `components/` into `plugins/`, and `plugins discord-bridge` in `server.cfg` |
| Both | contents of `include/` into your compiler's include folder |

- There is a single binary that works on both servers. SA-MP requires the
  **32-bit** build.
- On Linux, prefer the `static` package. `dynssl` needs OpenSSL installed on
  the machine. Keep any `libboost_system.so` next to the plugin.

In your script:

```pawn
#include <discord-bridge>
```

#### Existing Discord Connector scripts

Install the primary `discord-bridge.inc` from the bridge release. Download
`discord-dcc-compat.inc` and `discord-connector.inc` from
[omp-Discord-Bridge-compat](https://github.com/itsneufox/omp-Discord-Bridge-compat)
into the same compiler include directory, replacing the old
`discord-connector.inc`, and keep:

```pawn
#include <discord-connector>
```

You can also use `#include <discord-dcc-compat>` explicitly. Recompile your
scripts and load the matching **Discord Bridge** binary instead of Discord
Connector. Existing `.amx` files compiled against the original connector must
be recompiled. Configure the bot as described below.

The adapter covers DCC APIs with an existing bridge counterpart: names, tags,
enums, gateway callbacks, embeds, moderation and commands. Async callbacks keep
DCC's original argument order and `DCC_GetCreated*()` result getters. Slash
commands retain their optional `arguments` text field. `DCC_On*` maps to the
same public as `DBR_On*`; define each event only once per script.

These APIs have no bridge counterpart and are deliberately omitted:
`DCC_GetUserDiscriminator`, `DCC_GetInteractionMentionCount` and
`DCC_GetInteractionMention`. Code using them needs updating; this is source
compatibility for the supported subset, not full legacy binary compatibility.

### 2. Create the bot on Discord

1. Open the [Discord Developer Portal](https://discord.com/developers/applications)
   and click **New Application**.
2. In the **Bot** tab, click **Reset Token** and copy the token. Never share
   this token.
3. Still in **Bot**, enable under **Privileged Gateway Intents**:
   - **Server Members Intent** (member list, joins and leaves);
   - **Message Content Intent** (reading message text);
   - **Presence Intent** (members' online status).
4. In **OAuth2 > URL Generator**, tick the `bot` and `applications.commands`
   scopes, choose the permissions the bot needs and open the generated link to
   invite it to your server.

### 3. Configure the token

You can set the token in three places. When more than one is set, the first
one in this list wins:

1. the `DISCORD_BOT_TOKEN` environment variable;
2. the server configuration (`config.json` on open.mp, `server.cfg` on SA-MP);
3. `DBR_ConnectBot` in your gamemode.

With a token from 1 or 2 the bot connects by itself and `DBR_ConnectBot` is
ignored (the server log says so); choose the intents with `discord_bot_intents`
in that case.

**open.mp** (`config.json`):

```json
{
    "discord": {
        "bot_token": "YOUR_TOKEN",
        "channel_id": "123456789012345678"
    }
}
```

**SA-MP** (`server.cfg`):

```ini
discord_bot_token YOUR_TOKEN
discord_channel_id 123456789012345678
```

**Environment variable**:

```sh
export DISCORD_BOT_TOKEN="YOUR_TOKEN"
```

**Gamemode** (used only when neither of the above is set):

```pawn
public OnGameModeInit()
{
    DBR_ConnectBot("YOUR_TOKEN", DISCORD_INTENTS_DEFAULT);
    return 1;
}
```

| Setting | Environment variable | Purpose |
| --- | --- | --- |
| `discord_bot_token` | `DISCORD_BOT_TOKEN` | Bot token (required) |
| `discord_bot_intents` | `DISCORD_BOT_INTENTS` | Gateway intents (default: all; see [Choosing intents](#choosing-intents)) |
| `discord_channel_id` | `DISCORD_CHANNEL_ID` | Channel returned by `DBR_FindConfiguredChannel()` |
| `discord_channel_name` | `DISCORD_CHANNEL_NAME` | Same as above, by channel name |
| `discord_check_for_updates` | `DISCORD_CHECK_FOR_UPDATES` | Check GitHub for a newer stable release (default: enabled) |

Start the server. Once the bot connects, `DBR_OnReady` is called and
everything is ready to use.

At startup, the plugin checks GitHub for a newer stable release in the
background and logs a download link when one is available. The check is
best-effort and never downloads or installs files. To disable it, set
`discord_check_for_updates` to `false` in `config.json`, use
`discord_check_for_updates 0` in `server.cfg`, or set
`DISCORD_CHECK_FOR_UPDATES=0` in the environment.

To shut the bot down, call `DBR_DisconnectBot()`. It goes offline on Discord
right away and `DBR_OnDisconnected` is called; your commands stay registered,
so `DBR_ConnectBot` reconnects and publishes them again.

```pawn
public OnGameModeExit()
{
    DBR_DisconnectBot();
    return 1;
}
```

### Choosing intents

Intents tell Discord which events to send to the bot. Three of them are
**privileged** and must also be enabled in the Developer Portal (**Bot** tab >
**Privileged Gateway Intents**); if the bot asks for one that is not enabled,
Discord refuses the connection.

| Intent | Privileged | Needed for |
| --- | --- | --- |
| `DISCORD_INTENT_GUILDS` | | Servers, channels and roles (needed by almost everything) |
| `DISCORD_INTENT_GUILD_MEMBERS` | yes | Member list, `DBR_OnGuildMemberAdd/Update/Remove` |
| `DISCORD_INTENT_GUILD_PRESENCES` | yes | Members' online status |
| `DISCORD_INTENT_GUILD_MESSAGES` | | `DBR_OnMessageCreate/Update/Delete` in servers |
| `DISCORD_INTENT_MESSAGE_CONTENT` | yes | Reading the text of messages |
| `DISCORD_INTENT_GUILD_MESSAGE_REACTIONS` | | `DBR_OnMessageReaction` |
| `DISCORD_INTENT_GUILD_VOICE_STATES` | | `DBR_OnGuildMemberVoiceUpdate` |
| `DISCORD_INTENT_DIRECT_MESSAGES` | | Messages sent to the bot in DMs |

Presets:

| Preset | What it contains |
| --- | --- |
| `DISCORD_INTENTS_ALL` | Everything (default). Needs the three privileged intents enabled |
| `DISCORD_INTENTS_DEFAULT` | Everything that does not need the Developer Portal |
| `DISCORD_INTENTS_NONE` | No events. Slash commands, buttons and modals still work |

```pawn
// Works without enabling anything in the Developer Portal:
DBR_ConnectBot("YOUR_TOKEN", DISCORD_INTENTS_DEFAULT);

// Only what you use; reading message text needs Message Content enabled:
DBR_ConnectBot("YOUR_TOKEN", DISCORD_INTENT_GUILDS | DISCORD_INTENT_GUILD_MESSAGES | DISCORD_INTENT_MESSAGE_CONTENT);
```

When the token comes from the configuration, `discord_bot_intents` takes the
same value as a number: `53608447` for all intents, `53575421` for
`DISCORD_INTENTS_DEFAULT`.

## Concepts in 2 minutes

### Everything starts with `DBR_`

Every plugin function and callback uses the `DBR_` prefix (**D**iscord
**BR**idge). Type `DBR_` in your editor to see the whole API.

| Starts with | Meaning | Examples |
| --- | --- | --- |
| `DBR_Get...` / `DBR_Is...` | Reads data already loaded | `DBR_GetUserName`, `DBR_IsUserBot` |
| `DBR_Set...` | Changes something on Discord | `DBR_SetGuildMemberNickname` |
| `DBR_Create...` | Creates something (embed, button, command...) | `DBR_CreateEmbed` |
| `DBR_Fetch...` | Loads up-to-date data from Discord | `DBR_FetchGuildMember` |
| `DBR_On...` | Callback called by the plugin | `DBR_OnCommand` |

Constants and tags use `DISCORD_` and `Discord`: `DISCORD_BUTTON_PRIMARY`,
`DiscordUser:`.

### Handles

Servers, channels, users, roles and messages are represented by tagged
*handles* (`DiscordGuild:`, `DiscordChannel:`...). You can get a handle from an
ID at any time, even before the bot connects:

```pawn
new DiscordChannel:channel = DBR_FindChannelByID("123456789012345678");
```

- **Getters** (`DBR_Get...`) read what is already in memory and are instant.
- **Fetch** (`DBR_Fetch...`) loads data from Discord when it is not loaded yet.

### Asynchronous callbacks

Requests to Discord take a few milliseconds. Functions ending in
`callback[], format[], ...` call your function when the answer arrives. The
**first parameter** is what was created or loaded (0 on failure); your own
values follow:

```pawn
DBR_SendChannelMessage(channel, "Hello!", "OnSent", "i", playerid);

forward OnSent(DiscordMessage:message, playerid);
public OnSent(DiscordMessage:message, playerid)
{
    if (message == DISCORD_INVALID_MESSAGE) return 1; // failed
    // ...
    return 1;
}
```

Formats: `i`/`d` integer, `f` float, `b` boolean, `s` string, `a` array
followed by its size.

### Sending consumes

Embeds, message builders and modals are **destroyed when sent**. Build, send
and forget. Components added to a row, builder or modal belong to it.

### Errors

When Discord rejects something (missing permission, wrong ID, rate limit),
`DBR_OnActionFail` tells you why:

```pawn
public DBR_OnActionFail(const action[], http_status, error_code, const message[])
{
    printf("[discord] %s failed: %s", action, message);
    return 1;
}
```

### Debug mode

Informational messages, such as application commands being published, are
hidden by default. Turn them on while testing; warnings and errors are always
printed:

```pawn
DBR_SetDebugMode(true);
```

### Accents and text encoding

Discord uses UTF-8, while SA-MP and open.mp scripts usually store text as
Windows-1252. The plugin converts in both directions: accents you send appear
correctly on Discord, and accents received from Discord appear correctly in the
game. Characters that Windows-1252 cannot represent, such as emojis, arrive as
`?`. If your script's strings are UTF-8, turn the incoming conversion off:

```pawn
DBR_SetTextEncoding(DISCORD_ENCODING_UTF8);
```

## Quick guide

### Sending messages and embeds

```pawn
DBR_SendChannelMessage(channel, "Server online!");

new DiscordEmbed:embed = DBR_CreateEmbed("Status", "Everything is running.", .colour = 0x57F287);
DBR_AddEmbedField(embed, "Players", "12/100", true);
DBR_SendChannelEmbedMessage(channel, embed);
```

To combine text, several embeds and components, use a **message builder**:

```pawn
new DiscordMessageBuilder:message = DBR_CreateMessageBuilder("Read the rules:");
DBR_AddBuilderEmbed(message, DBR_CreateEmbed("Rules", "Respect everyone."));
DBR_AddBuilderComponent(message, buttonRow);
DBR_SendMessage(channel, message);
```

### Slash commands

```pawn
new DiscordCommand:cmd = DBR_CreateCommand("dice", "Rolls a dice", .callback = "Cmd_Dice");
new DiscordCommandOption:sides = DBR_AddCommandOption(cmd, DISCORD_OPTION_INTEGER, "sides", "Number of sides");
DBR_SetOptionRange(sides, 2.0, 100.0);

forward Cmd_Dice(DiscordInteraction:interaction, DiscordUser:user);
public Cmd_Dice(DiscordInteraction:interaction, DiscordUser:user)
{
    new sides = 6;
    DBR_GetInteractionOptionInt(interaction, "sides", sides);
    // ...
}
```

- Create commands whenever you like. The plugin publishes them once the bot is
  ready and does not publish again when nothing changed.
- Commands without `.callback` arrive in `DBR_OnCommand`.
- Pass a server (`DBR_FindGuildByID("ID")`) to register the command there only:
  it shows up immediately, great for testing. Global commands may take a few
  minutes.

> Each scope (global or one server) is **replaced as a whole**. Commands created
> by other tools for the same bot and scope are removed.

### Responding to interactions

Every interaction (command, button, menu, modal) needs a response:

| Function | When to use |
| --- | --- |
| `DBR_RespondInteraction(i, "text", .ephemeral = true)` | Quick response (private with `ephemeral`) |
| `DBR_RespondInteractionEmbed` / `DBR_RespondInteractionMessage` | Response with an embed or builder |
| `DBR_DeferInteraction(i)` | Taking a while? Show "thinking..." and respond later |
| `DBR_UpdateInteractionMessage(i, builder)` | Buttons and menus: replace the message they were clicked on |
| `DBR_SendInteractionFollowup` | Extra message after the response |
| `DBR_EditInteractionResponse` / `DBR_DeleteInteractionResponse` | Edit or delete the response |
| `DBR_ShowModal(i, modal)` | Open a form |

If you do not respond, the plugin acknowledges the interaction when the
callback returns. The handle lasts 15 minutes, so you can respond after a
timer or a database query.

### Buttons and menus

```pawn
new DiscordComponent:row = DBR_CreateActionRow();
DBR_AddComponent(row, DBR_CreateButton(DISCORD_BUTTON_SUCCESS, "Accept", "accept"));
DBR_AddComponent(row, DBR_CreateButton(DISCORD_BUTTON_LINK, "Website", "https://open.mp"));

public DBR_OnButton(DiscordInteraction:interaction, DiscordUser:user, const custom_id[])
{
    if (!strcmp(custom_id, "accept"))
        return DBR_RespondInteraction(interaction, "Thanks!", .ephemeral = true);
    return 1;
}
```

Menus use `DBR_CreateSelectMenu` and arrive in `DBR_OnSelectMenu`. Read the
picked values with `DBR_GetInteractionValue(interaction, "", value)`.

To organise many buttons, register a prefix handler:

```pawn
DBR_RegisterHandler(DISCORD_INTERACTION_COMPONENT, "shop:", "OnShopClick", true);
```

### Modals

```pawn
new DiscordModal:modal = DBR_CreateModal("report", "Report a player");
DBR_AddModalTextInput(modal, "nick", "Player nickname");
DBR_AddModalTextInput(modal, "reason", "What happened?", DISCORD_TEXT_INPUT_PARAGRAPH);
DBR_ShowModal(interaction, modal);

public DBR_OnModalSubmit(DiscordInteraction:interaction, DiscordUser:user, const custom_id[])
{
    new nick[25];
    DBR_GetInteractionValue(interaction, "nick", nick);
    return DBR_RespondInteraction(interaction, "Report received!", .ephemeral = true);
}
```

### Components V2

Add blocks such as `DBR_CreateContainer`, `DBR_CreateSection`,
`DBR_CreateTextDisplay`, `DBR_CreateMediaGallery` and `DBR_CreateSeparator` to
a message builder. The message becomes Components V2 automatically: the
builder text becomes the first block and embeds are not allowed. See
[example 07](examples/en/07-components-v2.pwn).

### Moderation

```pawn
DBR_KickGuildMember(server, member, "Reason");
DBR_BanGuildMember(server, member, "Reason", .delete_message_seconds = 86400);
DBR_UnbanGuildMember(server, DBR_FindUserByID("123..."));
DBR_SetGuildMemberTimeout(server, member, 600, "Spam");   // 10 minutes
DBR_SetGuildMemberNickname(server, member, "New nickname");
DBR_AddGuildMemberRole(server, member, role);
```

The bot's role must be **above** the role of the member it acts on. Reasons
show up in the audit log.

### Lookups

```pawn
DBR_FetchGuildMember(server, member, "OnLoaded");

forward OnLoaded(DiscordGuild:server, DiscordUser:member);
public OnLoaded(DiscordGuild:server, DiscordUser:member)
{
    new name[33], joined[33];
    DBR_GetGuildMemberDisplayName(server, member, name);
    DBR_GetGuildMemberJoinedAt(server, member, joined);
    return 1;
}
```

There are also `DBR_FetchGuild`, `DBR_FetchUser`, `DBR_FetchRole`,
`DBR_FetchChannel` and `DBR_FetchMessage`.

### Bot profile

```pawn
DBR_SetBotPresenceStatus(DISCORD_BOT_PRESENCE_IDLE);
DBR_SetBotActivity("12 players online", DISCORD_ACTIVITY_WATCHING);
DBR_SetBotAvatar("bot/avatar.png");       // also looked up in scriptfiles/
DBR_SetBotUsername("My Server");
```

## Callbacks

| Callback | When it is called |
| --- | --- |
| `DBR_OnReady()` | Bot connected and servers loaded |
| `DBR_OnDisconnected()` | Connection lost (the plugin reconnects by itself) |
| `DBR_OnActionFail(action[], http_status, error_code, message[])` | Discord rejected an action |
| `DBR_OnInteraction(interaction, user, type)` | Before any interaction; return `0` to block it |
| `DBR_OnCommand(interaction, user, command[])` | Command without its own callback |
| `DBR_OnButton(interaction, user, custom_id[])` | Button click |
| `DBR_OnSelectMenu(interaction, user, custom_id[])` | Menu selection |
| `DBR_OnModalSubmit(interaction, user, custom_id[])` | Modal submission |
| `DBR_OnAutocomplete(interaction, user, command[], option[])` | Typing in an option with autocomplete |
| `DBR_OnMessageCreate/Update/Delete(message)` | Messages created, edited or deleted |
| `DBR_OnMessageReaction(message, user, emoji, type)` | Reactions |
| `DBR_OnGuildMemberAdd/Update/Remove(guild, user)` | Members joining, changing or leaving |
| `DBR_OnGuildMemberVoiceUpdate(guild, user, channel)` | Joining and leaving voice channels |
| `DBR_OnGuildCreate/Update/Delete(guild)` | Servers |
| `DBR_OnGuildRoleCreate/Update/Delete(guild, role)` | Roles |
| `DBR_OnChannelCreate/Update/Delete(channel)` | Channels |
| `DBR_OnUserUpdate(user)` | A user's profile changed |

An interaction goes through this order: `DBR_OnInteraction` → the command
callback or a handler registered with `DBR_RegisterHandler` →
`DBR_OnCommand` / `DBR_OnButton` / `DBR_OnSelectMenu` / `DBR_OnModalSubmit` /
`DBR_OnAutocomplete`.

The full list of functions, with parameters, is in the `discord-bridge.inc`
include.

## Troubleshooting

**The bot does not connect.** Check the token and whether the privileged
intents are enabled in the Developer Portal. With the default (all intents)
the three privileged ones must be on. To connect without them, use
`DBR_ConnectBot(token, DISCORD_INTENTS_DEFAULT)` or `discord_bot_intents 53575421`.
The server log names the missing intent when Discord refuses the connection.

**The command does not show up.** Global commands may take a few minutes.
Register it in your server while testing and make sure the bot was invited
with the `applications.commands` scope. Try `Ctrl+R` in Discord.

**"The application did not respond".** Discord expects a response within 3
seconds. Do not do heavy work before responding: call `DBR_DeferInteraction`
and respond later.

**Message text arrives empty.** Enable the **Message Content Intent**.

**Kick/ban does not work.** Check `DBR_OnActionFail`. Code `50013` means
missing permission or the bot's role is below the target.

**Getters return 0.** The data is not in memory yet. Wait for `DBR_OnReady` or
use the matching `DBR_Fetch...` function.

**`Could NOT find OpenSSL` when configuring.** The dependencies are not
installed where CMake can see them. On Windows, build with the vcpkg preset
(`cmake --preset windows-x86`) instead of `cmake -S . -B build`, and make sure
`VCPKG_ROOT` is set. On Linux, install `libssl-dev` and `libboost-system-dev`.

## Building the plugin

Clone the repository recursively so the open.mp and AMX SDKs come along:

```sh
git clone --recursive https://github.com/itsneufox/omp-Discord-Bridge
cd omp-Discord-Bridge
```

### Windows

The dependencies (OpenSSL and Boost) are installed automatically by
[vcpkg](https://github.com/microsoft/vcpkg) from `vcpkg.json`. You only need
**Visual Studio 2022** with the *Desktop development with C++* workload,
**CMake 3.21+** and vcpkg.

1. Install vcpkg once. Keep it in a short path such as `C:\vcpkg`: deep
   folders hit the Windows 260-character path limit while dependencies build.

   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   setx VCPKG_ROOT C:\vcpkg
   ```

   Close and reopen the terminal so `VCPKG_ROOT` is available.

2. Build. The first run downloads and compiles OpenSSL and Boost, which takes
   about 10 to 15 minutes; later builds are fast.

   ```powershell
   cmake --preset windows-x86
   cmake --build --preset windows-x86
   ctest --preset windows-x86
   ```

Outputs in `build/windows-x86/`:

- `plugins/Release/discord-bridge.dll`: the plugin, with OpenSSL built in, so it
  is the only file to copy (`components/` on open.mp, `plugins/` on SA-MP);
- `pawno/include/discord-bridge.inc`: the primary include. Compatibility includes
  are distributed separately in [omp-Discord-Bridge-compat](https://github.com/itsneufox/omp-Discord-Bridge-compat).

Windows servers are 32-bit, so the preset always builds for Win32.

### Linux

```sh
sudo apt install build-essential cmake libssl-dev libboost-system-dev
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Outputs: `build/plugins/discord-bridge.so` and `build/pawno/include/discord-bridge.inc`.
For the 32-bit build used by SA-MP, add `-DDISCORD_BRIDGE_BUILD_32BIT=ON` and
install the `:i386` versions of the libraries (see `.github/workflows/build.yml`).

Set `-DDISCORD_BRIDGE_VERSION=X.Y.Z` on either platform to choose the version.

## Batch rate-limited chat (opt-in)

Batching is **off by default**. Keep using the ordinary send function:

```pawn
DBR_SendChannelMessage(chatChannel, "[Player] Hello!");
```

To enable batching in open.mp's `config.json`:

```json
{
  "discord_batch_rate_limited": true,
  "discord_batch_interval_ms": 5000
}
```

For SA-MP, in `server.cfg`:

```text
discord_batch_rate_limited 1
discord_batch_interval_ms 5000
```

With the option enabled, normal chat is sent immediately through the REST queue.
When Discord's rate-limit headers or a 429 response delay a send, pending plain
text messages for that channel are joined with newlines. The first deferred send
starts a **5,000 ms (5 second)** window; later messages do not restart it. Sending
waits for both that window and Discord's retry deadline. A longer Discord limit
can therefore delay delivery beyond five seconds. Once the backlog is sent,
normal immediate sending resumes. Discord-to-game messages continue through the
Gateway as usual; batching does not pause incoming chat or the game thread.

Set `discord_batch_rate_limited` to `false` (SA-MP: `0`) to disable batching.
The dotted aliases `discord.batch_rate_limited` and `discord.batch_interval_ms`
are also accepted. Environment variables `DISCORD_BATCH_RATE_LIMITED` and
`DISCORD_BATCH_INTERVAL_MS` take priority over their respective config settings.
For the environment toggle, use `true` or `1` to enable; other values disable it.
The interval must be a positive integer in milliseconds; invalid values fall back
to 5000. Setting an interval alone does **not** enable batching. Restart the server
after changing configuration.

Each combined message is limited to 2,000 UTF-8 bytes, keeping each original
message intact and preserving channel order. Sends with completion callbacks
remain separate so each callback receives its own message result. Embeds,
interactions and other REST operations are not combined, and intervening REST
operations may separate batches. Unblocked channels can continue sending.

The REST queue accepts up to 8,192 pending original requests per bot, including
requests combined into batches. A return value of `1` means queued, not delivered;
`0` means invalid input, a stopped bot or a full queue. HTTP failures are logged.
Disconnecting discards queued messages. No separate batching native is needed;
the compatibility include's `DCC_SendChannelMessage` uses the same behavior.

## AI disclosure

AI tools assisted with parts of the code and documentation. Review the source
and test the plugin on your own server before using it in production.

## License

Licensed under the [MIT License](LICENSE). Bundled third-party code retains its
own licenses.
