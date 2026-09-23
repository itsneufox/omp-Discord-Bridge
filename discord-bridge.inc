#if defined _discord_bridge_included
	#endinput
#endif
#define _discord_bridge_included

#define DISCORD_BRIDGE_VERSION "@PROJECT_VERSION@"
#define DISCORD_BRIDGE_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define DISCORD_BRIDGE_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define DISCORD_BRIDGE_VERSION_PATCH @PROJECT_VERSION_PATCH@

/*
	Discord Bridge

	Naming
	  Every native and callback starts with DBR_ (Discord BRidge): DBR_Get*,
	  DBR_Set*, DBR_Create*, DBR_Fetch* and DBR_On*.  Tags use Discord* and
	  constants use DISCORD_*.

	Handles
	  Guild, channel, user, role and message handles are backed by their Discord
	  snowflake, so a handle resolved with DBR_Find*ByID can be used before the
	  gateway cache knows the entity.  Getters read the cache and return 0 when
	  the entity is not cached; DBR_Fetch* natives load it through the REST API.

	Asynchronous callbacks
	  Natives with `callback[], format[], {Float, _}:...` run the callback once
	  Discord answers.  The entity the request produced is passed first, then
	  your own arguments (format: d/i/f/b = cell, s = string, a = array followed
	  by its size, r = reference to global data).  A failed request passes 0.

	Ownership
	  Sending consumes embeds, message builders and modals: the handle is
	  destroyed by the call, even when it fails.  Components added to a parent,
	  a builder or a modal are owned by it.

	Errors
	  Failed REST actions call DBR_OnActionFail(action[], http_status,
	  error_code, message[]).
*/

#define DISCORD_INVALID_CHANNEL DiscordChannel:0
#define DISCORD_INVALID_USER DiscordUser:0
#define DISCORD_INVALID_ROLE DiscordRole:0
#define DISCORD_INVALID_GUILD DiscordGuild:0
#define DISCORD_INVALID_MESSAGE DiscordMessage:0
#define DISCORD_INVALID_INTERACTION DiscordInteraction:0
#define DISCORD_INVALID_COMMAND DiscordCommand:0
#define DISCORD_INVALID_COMMAND_OPTION DiscordCommandOption:0
#define DISCORD_INVALID_COMPONENT DiscordComponent:0
#define DISCORD_INVALID_MESSAGE_BUILDER DiscordMessageBuilder:0
#define DISCORD_INVALID_MODAL DiscordModal:0
#define DISCORD_INVALID_EMBED DiscordEmbed:0

#define DISCORD_ID_SIZE (20 + 1)
#define DISCORD_USERNAME_SIZE (32 + 1)
#define DISCORD_NICKNAME_SIZE (32 + 1)
#define DISCORD_EMOJI_NAME_SIZE (32 + 1)
#define DISCORD_COMMAND_NAME_SIZE (32 + 1)
#define DISCORD_CUSTOM_ID_SIZE (100 + 1)
#define DISCORD_MESSAGE_SIZE (2000 + 1)
#define DISCORD_URL_SIZE (512 + 1)
#define DISCORD_TIMESTAMP_SIZE (32 + 1)

// -----------------------------------------------------------------------------
// Enumerations
// -----------------------------------------------------------------------------

enum DiscordChannelType
{
	DISCORD_GUILD_TEXT = 0,
	DISCORD_DM = 1,
	DISCORD_GUILD_VOICE = 2,
	DISCORD_GROUP_DM = 3,
	DISCORD_GUILD_CATEGORY = 4,
	DISCORD_GUILD_NEWS = 5,
	DISCORD_GUILD_STORE = 6,
	DISCORD_GUILD_NEWS_THREAD = 10,
	DISCORD_GUILD_PUBLIC_THREAD = 11,
	DISCORD_GUILD_PRIVATE_THREAD = 12,
	DISCORD_GUILD_STAGE_VOICE = 13,
	DISCORD_GUILD_DIRECTORY = 14,
	DISCORD_GUILD_FORUM = 15,
	DISCORD_GUILD_MEDIA = 16
};

enum DiscordUserPresenceStatus
{
	DISCORD_PRESENCE_INVALID = 0,
	DISCORD_PRESENCE_ONLINE = 1,
	DISCORD_PRESENCE_IDLE = 2,
	DISCORD_PRESENCE_DO_NOT_DISTURB = 3,
	DISCORD_PRESENCE_OFFLINE = 4
};

enum DiscordBotPresenceStatus
{
	DISCORD_BOT_PRESENCE_INVALID = 0,
	DISCORD_BOT_PRESENCE_ONLINE,
	DISCORD_BOT_PRESENCE_IDLE,
	DISCORD_BOT_PRESENCE_DO_NOT_DISTURB,
	DISCORD_BOT_PRESENCE_INVISIBLE,
	DISCORD_BOT_PRESENCE_OFFLINE
};

enum DiscordActivityType
{
	DISCORD_ACTIVITY_PLAYING = 0,
	DISCORD_ACTIVITY_STREAMING = 1,
	DISCORD_ACTIVITY_LISTENING = 2,
	DISCORD_ACTIVITY_WATCHING = 3,
	DISCORD_ACTIVITY_CUSTOM = 4,
	DISCORD_ACTIVITY_COMPETING = 5
};

enum DiscordMessageReactionType
{
	DISCORD_REACTION_ADD = 0,
	DISCORD_REACTION_REMOVE,
	DISCORD_REACTION_REMOVE_ALL,
	DISCORD_REACTION_REMOVE_EMOJI
};

enum DiscordInteractionType
{
	DISCORD_INTERACTION_PING = 1,
	DISCORD_INTERACTION_COMMAND = 2,
	DISCORD_INTERACTION_COMPONENT = 3,
	DISCORD_INTERACTION_AUTOCOMPLETE = 4,
	DISCORD_INTERACTION_MODAL_SUBMIT = 5
};

enum DiscordCommandType
{
	DISCORD_COMMAND_SLASH = 1,
	DISCORD_COMMAND_USER = 2,
	DISCORD_COMMAND_MESSAGE = 3
};

enum DiscordCommandOptionType
{
	DISCORD_OPTION_SUBCOMMAND = 1,
	DISCORD_OPTION_SUBCOMMAND_GROUP = 2,
	DISCORD_OPTION_STRING = 3,
	DISCORD_OPTION_INTEGER = 4,
	DISCORD_OPTION_BOOLEAN = 5,
	DISCORD_OPTION_USER = 6,
	DISCORD_OPTION_CHANNEL = 7,
	DISCORD_OPTION_ROLE = 8,
	DISCORD_OPTION_MENTIONABLE = 9,
	DISCORD_OPTION_NUMBER = 10,
	DISCORD_OPTION_ATTACHMENT = 11
};

enum DiscordComponentType
{
	DISCORD_COMPONENT_ACTION_ROW = 1,
	DISCORD_COMPONENT_BUTTON = 2,
	DISCORD_COMPONENT_STRING_SELECT = 3,
	DISCORD_COMPONENT_TEXT_INPUT = 4,
	DISCORD_COMPONENT_USER_SELECT = 5,
	DISCORD_COMPONENT_ROLE_SELECT = 6,
	DISCORD_COMPONENT_MENTIONABLE_SELECT = 7,
	DISCORD_COMPONENT_CHANNEL_SELECT = 8,
	DISCORD_COMPONENT_SECTION = 9,
	DISCORD_COMPONENT_TEXT_DISPLAY = 10,
	DISCORD_COMPONENT_THUMBNAIL = 11,
	DISCORD_COMPONENT_MEDIA_GALLERY = 12,
	DISCORD_COMPONENT_FILE = 13,
	DISCORD_COMPONENT_SEPARATOR = 14,
	DISCORD_COMPONENT_CONTAINER = 17,
	DISCORD_COMPONENT_LABEL = 18,
	DISCORD_COMPONENT_FILE_UPLOAD = 19
};

enum DiscordButtonStyle
{
	DISCORD_BUTTON_PRIMARY = 1,
	DISCORD_BUTTON_SECONDARY = 2,
	DISCORD_BUTTON_SUCCESS = 3,
	DISCORD_BUTTON_DANGER = 4,
	DISCORD_BUTTON_LINK = 5,
	DISCORD_BUTTON_PREMIUM = 6
};

enum DiscordSelectMenuType
{
	DISCORD_SELECT_STRING = 3,
	DISCORD_SELECT_USER = 5,
	DISCORD_SELECT_ROLE = 6,
	DISCORD_SELECT_MENTIONABLE = 7,
	DISCORD_SELECT_CHANNEL = 8
};

enum DiscordSelectDefaultType
{
	DISCORD_SELECT_DEFAULT_USER = 1,
	DISCORD_SELECT_DEFAULT_ROLE = 2,
	DISCORD_SELECT_DEFAULT_CHANNEL = 3
};

enum DiscordTextInputStyle
{
	DISCORD_TEXT_INPUT_SHORT = 1,
	DISCORD_TEXT_INPUT_PARAGRAPH = 2
};

enum DiscordSeparatorSpacing
{
	DISCORD_SPACING_SMALL = 1,
	DISCORD_SPACING_LARGE = 2
};

// Permission bit indexes, used by DBR_HasInteractionPermission.
enum DiscordPermission
{
	DISCORD_PERMISSION_CREATE_INVITE = 0,
	DISCORD_PERMISSION_KICK_MEMBERS = 1,
	DISCORD_PERMISSION_BAN_MEMBERS = 2,
	DISCORD_PERMISSION_ADMINISTRATOR = 3,
	DISCORD_PERMISSION_MANAGE_CHANNELS = 4,
	DISCORD_PERMISSION_MANAGE_GUILD = 5,
	DISCORD_PERMISSION_ADD_REACTIONS = 6,
	DISCORD_PERMISSION_VIEW_AUDIT_LOG = 7,
	DISCORD_PERMISSION_VIEW_CHANNEL = 10,
	DISCORD_PERMISSION_SEND_MESSAGES = 11,
	DISCORD_PERMISSION_MANAGE_MESSAGES = 13,
	DISCORD_PERMISSION_EMBED_LINKS = 14,
	DISCORD_PERMISSION_ATTACH_FILES = 15,
	DISCORD_PERMISSION_READ_HISTORY = 16,
	DISCORD_PERMISSION_MENTION_EVERYONE = 17,
	DISCORD_PERMISSION_CONNECT = 20,
	DISCORD_PERMISSION_SPEAK = 21,
	DISCORD_PERMISSION_MUTE_MEMBERS = 22,
	DISCORD_PERMISSION_DEAFEN_MEMBERS = 23,
	DISCORD_PERMISSION_MOVE_MEMBERS = 24,
	DISCORD_PERMISSION_CHANGE_NICKNAME = 26,
	DISCORD_PERMISSION_MANAGE_NICKNAMES = 27,
	DISCORD_PERMISSION_MANAGE_ROLES = 28,
	DISCORD_PERMISSION_MANAGE_WEBHOOKS = 29,
	DISCORD_PERMISSION_MODERATE_MEMBERS = 40
};

// Values for DBR_SetCommandPermissions.
#define DISCORD_COMMAND_EVERYONE ""
#define DISCORD_COMMAND_ADMINISTRATORS "8"
#define DISCORD_COMMAND_MODERATORS "1099511627776"

// -----------------------------------------------------------------------------
// Connection
// -----------------------------------------------------------------------------

// Gateway intents choose which events Discord sends to the bot.  Combine them
// with |, for example DISCORD_INTENT_GUILDS | DISCORD_INTENT_GUILD_MESSAGES.
// Intents marked [privileged] must also be enabled in the Discord Developer
// Portal (Bot tab > Privileged Gateway Intents), otherwise Discord refuses the
// connection.
enum DiscordIntent (<<= 1)
{
	DISCORD_INTENT_GUILDS = 1,                  // servers, channels and roles (needed by almost everything)
	DISCORD_INTENT_GUILD_MEMBERS,               // [privileged] member list, joins, leaves and updates
	DISCORD_INTENT_GUILD_MODERATION,            // bans and audit log entries
	DISCORD_INTENT_GUILD_EXPRESSIONS,           // emojis, stickers and soundboard sounds
	DISCORD_INTENT_GUILD_INTEGRATIONS,          // integration updates
	DISCORD_INTENT_GUILD_WEBHOOKS,              // webhook updates
	DISCORD_INTENT_GUILD_INVITES,               // invite creation and deletion
	DISCORD_INTENT_GUILD_VOICE_STATES,          // voice channel joins, leaves and moves
	DISCORD_INTENT_GUILD_PRESENCES,             // [privileged] online status and activities
	DISCORD_INTENT_GUILD_MESSAGES,              // messages sent in servers
	DISCORD_INTENT_GUILD_MESSAGE_REACTIONS,     // reactions in servers
	DISCORD_INTENT_GUILD_MESSAGE_TYPING,        // typing indicators in servers
	DISCORD_INTENT_DIRECT_MESSAGES,             // direct messages to the bot
	DISCORD_INTENT_DIRECT_MESSAGE_REACTIONS,    // reactions in direct messages
	DISCORD_INTENT_DIRECT_MESSAGE_TYPING,       // typing indicators in direct messages
	DISCORD_INTENT_MESSAGE_CONTENT,             // [privileged] text, embeds and attachments of messages
	DISCORD_INTENT_GUILD_SCHEDULED_EVENTS,      // scheduled events
	DISCORD_INTENT_AUTOMOD_CONFIG = 1048576,    // AutoMod rule changes
	DISCORD_INTENT_AUTOMOD_EXECUTION,           // AutoMod actions
	DISCORD_INTENT_GUILD_MESSAGE_POLLS = 16777216, // poll votes in servers
	DISCORD_INTENT_DIRECT_MESSAGE_POLLS         // poll votes in direct messages
};

// No events at all.  Slash commands, buttons and modals still work.
#define DISCORD_INTENTS_NONE (DiscordIntent:0)
// The three intents that must be enabled in the Developer Portal.
#define DISCORD_INTENTS_PRIVILEGED (DISCORD_INTENT_GUILD_MEMBERS | DISCORD_INTENT_GUILD_PRESENCES | DISCORD_INTENT_MESSAGE_CONTENT)
// Every intent.  Requires the three privileged intents in the Developer Portal.
#define DISCORD_INTENTS_ALL (DiscordIntent:53608447)
// Every intent that works without touching the Developer Portal.
#define DISCORD_INTENTS_DEFAULT (DiscordIntent:53575421)

// Connects the bot from the gamemode.  A token set outside the script (the
// DISCORD_BOT_TOKEN environment variable or discord_bot_token in config.json /
// server.cfg) takes priority: the bot then connects by itself and this call is
// ignored with a warning.  Priority: environment variable > config > gamemode.
native bool:DBR_ConnectBot(const token[], DiscordIntent:intents = DISCORD_INTENTS_ALL);
native bool:DBR_IsConnected();
// Shuts the bot down: it goes offline on Discord right away and
// DBR_OnDisconnected is called if it was connected.  Safe in OnGameModeExit.
// Called inside a Discord callback (DBR_OnCommand, DBR_OnButton...), it
// happens when that callback returns.  Commands, handlers and handles are
// kept; DBR_ConnectBot reconnects and publishes the commands again.
// Returns 0 when no bot is running.
native DBR_DisconnectBot();

// Debug mode prints informational messages, such as application commands being
// published.  It is off by default; warnings and errors are always printed.
native DBR_SetDebugMode(bool:enabled);
native bool:DBR_IsDebugMode();

// How the script's strings are encoded.  ANSI (Windows-1252, the default)
// suits SA-MP and open.mp gamemodes with accents: text sent to Discord is
// converted to UTF-8, and text received from Discord is converted back, with
// characters Windows-1252 lacks (such as emojis) becoming '?'.  Choose UTF-8
// when the script's strings are UTF-8.
enum DiscordTextEncoding
{
	DISCORD_ENCODING_ANSI = 0,
	DISCORD_ENCODING_UTF8 = 1
};

native DBR_SetTextEncoding(DiscordTextEncoding:encoding);
native DiscordTextEncoding:DBR_GetTextEncoding();

// -----------------------------------------------------------------------------
// Channels
// -----------------------------------------------------------------------------

native DiscordChannel:DBR_FindChannelByID(const channel_id[]);
native DiscordChannel:DBR_FindChannelByName(const channel_name[]);
native DiscordChannel:DBR_FindConfiguredChannel();
native DBR_FetchChannel(DiscordChannel:channel, const callback[], const format[] = "", {Float, _}:...);
native DBR_GetChannelID(DiscordChannel:channel, dest[DISCORD_ID_SIZE], max_size = sizeof dest);
native DBR_GetChannelType(DiscordChannel:channel, &DiscordChannelType:type);
native DBR_GetChannelGuild(DiscordChannel:channel, &DiscordGuild:guild);
native DBR_GetChannelName(DiscordChannel:channel, dest[], max_size = sizeof dest);
native DBR_GetChannelTopic(DiscordChannel:channel, dest[], max_size = sizeof dest);
native DBR_GetChannelPosition(DiscordChannel:channel, &position);
native DBR_IsChannelNsfw(DiscordChannel:channel, &bool:is_nsfw);
native DBR_GetChannelParentCategory(DiscordChannel:channel, &DiscordChannel:category);
native DBR_GetChannelSlowmode(DiscordChannel:channel, &seconds);
// Optional discord_batch_rate_limited batches plain sends only when rate-limited.
// Disabled by default; callback sends stay separate. See server configuration.
native DBR_SendChannelMessage(DiscordChannel:channel, const message[], const callback[] = "", const format[] = "", {Float, _}:...);
native DBR_SetChannelName(DiscordChannel:channel, const name[]);
native DBR_SetChannelTopic(DiscordChannel:channel, const topic[]);
native DBR_SetChannelPosition(DiscordChannel:channel, position);
native DBR_SetChannelNsfw(DiscordChannel:channel, bool:is_nsfw);
native DBR_SetChannelParentCategory(DiscordChannel:channel, DiscordChannel:parent_category);
native DBR_SetChannelSlowmode(DiscordChannel:channel, seconds);
native DBR_DeleteChannel(DiscordChannel:channel);
native DBR_TriggerBotTypingIndicator(DiscordChannel:channel);

// -----------------------------------------------------------------------------
// Users
// -----------------------------------------------------------------------------

native DiscordUser:DBR_FindUserByID(const user_id[]);
// The discriminator is optional and only applies to legacy accounts.
native DiscordUser:DBR_FindUserByName(const user_name[], const discriminator[] = "");
native DBR_FetchUser(DiscordUser:user, const callback[], const format[] = "", {Float, _}:...);
native DBR_GetUserID(DiscordUser:user, dest[DISCORD_ID_SIZE], max_size = sizeof dest);
native DBR_GetUserName(DiscordUser:user, dest[], max_size = sizeof dest);
// Returns the cached discriminator string unchanged (including "0" when supplied).
native DBR_GetUserDiscriminator(DiscordUser:user, dest[], max_size = sizeof dest);
native DBR_GetUserGlobalName(DiscordUser:user, dest[], max_size = sizeof dest);
native DBR_GetUserAvatarURL(DiscordUser:user, dest[], max_size = sizeof dest, size = 256);
native DBR_GetUserBannerURL(DiscordUser:user, dest[], max_size = sizeof dest, size = 256);
native DBR_IsUserBot(DiscordUser:user, &bool:is_bot);
native DBR_IsUserVerified(DiscordUser:user, &bool:is_verified);
native DBR_IsUserSystem(DiscordUser:user, &bool:is_system);
// callback(DiscordChannel:channel, ...)
native DBR_CreatePrivateChannel(DiscordUser:user, const callback[], const format[] = "", {Float, _}:...);

// -----------------------------------------------------------------------------
// Guilds
// -----------------------------------------------------------------------------

native DiscordGuild:DBR_FindGuildByID(const guild_id[]);
native DiscordGuild:DBR_FindGuildByName(const guild_name[]);
native DBR_FetchGuild(DiscordGuild:guild, const callback[], const format[] = "", {Float, _}:...);
native DBR_GetGuildID(DiscordGuild:guild, dest[DISCORD_ID_SIZE], max_size = sizeof dest);
native DBR_GetGuildName(DiscordGuild:guild, dest[], max_size = sizeof dest);
native DBR_GetGuildOwnerID(DiscordGuild:guild, dest[DISCORD_ID_SIZE], max_size = sizeof dest);
native DBR_GetGuildDescription(DiscordGuild:guild, dest[], max_size = sizeof dest);
native DBR_GetGuildIcon(DiscordGuild:guild, dest[], max_size = sizeof dest);
native DBR_GetGuildIconURL(DiscordGuild:guild, dest[], max_size = sizeof dest, size = 256);
native DBR_GetGuildBannerURL(DiscordGuild:guild, dest[], max_size = sizeof dest, size = 256);
native DBR_GetGuildTotalMembers(DiscordGuild:guild, &count);
native DBR_GetGuildRole(DiscordGuild:guild, offset, &DiscordRole:role);
native DBR_GetGuildRoleCount(DiscordGuild:guild, &count);
native DBR_GetGuildMember(DiscordGuild:guild, offset, &DiscordUser:user);
// Number of members currently cached, see DBR_GetGuildTotalMembers.
native DBR_GetGuildMemberCount(DiscordGuild:guild, &count);
native DBR_GetGuildChannel(DiscordGuild:guild, offset, &DiscordChannel:channel);
native DBR_GetGuildChannelCount(DiscordGuild:guild, &count);
native DBR_GetAllGuilds(DiscordGuild:dest[], max_size = sizeof dest);
native DBR_SetGuildName(DiscordGuild:guild, const name[]);
// callback(DiscordChannel:channel, ...)
native DBR_CreateGuildChannel(DiscordGuild:guild, const name[], DiscordChannelType:type, const callback[] = "", const format[] = "", {Float, _}:...);

// -----------------------------------------------------------------------------
// Guild members
// -----------------------------------------------------------------------------

// callback(DiscordGuild:guild, DiscordUser:user, ...)
native DBR_FetchGuildMember(DiscordGuild:guild, DiscordUser:user, const callback[], const format[] = "", {Float, _}:...);
native DBR_GetGuildMemberNickname(DiscordGuild:guild, DiscordUser:user, dest[DISCORD_NICKNAME_SIZE], max_size = sizeof dest);
native DBR_GetGuildMemberDisplayName(DiscordGuild:guild, DiscordUser:user, dest[], max_size = sizeof dest);
native DBR_GetGuildMemberAvatarURL(DiscordGuild:guild, DiscordUser:user, dest[], max_size = sizeof dest, size = 256);
native DBR_GetGuildMemberJoinedAt(DiscordGuild:guild, DiscordUser:user, dest[DISCORD_TIMESTAMP_SIZE], max_size = sizeof dest);
native DBR_GetGuildMemberPremiumSince(DiscordGuild:guild, DiscordUser:user, dest[DISCORD_TIMESTAMP_SIZE], max_size = sizeof dest);
// Seconds left in the member's timeout, 0 when not timed out.
native DBR_GetGuildMemberTimeout(DiscordGuild:guild, DiscordUser:user, &seconds_left);
native DBR_IsGuildMemberPending(DiscordGuild:guild, DiscordUser:user, &bool:is_pending);
native DBR_IsGuildMemberMuted(DiscordGuild:guild, DiscordUser:user, &bool:is_muted);
native DBR_IsGuildMemberDeafened(DiscordGuild:guild, DiscordUser:user, &bool:is_deafened);
native DBR_GetGuildMemberVoiceChannel(DiscordGuild:guild, DiscordUser:user, &DiscordChannel:channel);
native DBR_GetGuildMemberRole(DiscordGuild:guild, DiscordUser:user, offset, &DiscordRole:role);
native DBR_GetGuildMemberRoleCount(DiscordGuild:guild, DiscordUser:user, &count);
native DBR_HasGuildMemberRole(DiscordGuild:guild, DiscordUser:user, DiscordRole:role, &bool:has_role);
native DBR_GetGuildMemberStatus(DiscordGuild:guild, DiscordUser:user, &DiscordUserPresenceStatus:status);

native DBR_KickGuildMember(DiscordGuild:guild, DiscordUser:user, const reason[] = "");
native DBR_BanGuildMember(DiscordGuild:guild, DiscordUser:user, const reason[] = "", delete_message_seconds = 0);
native DBR_UnbanGuildMember(DiscordGuild:guild, DiscordUser:user, const reason[] = "");
// An empty nickname resets it.
native DBR_SetGuildMemberNickname(DiscordGuild:guild, DiscordUser:user, const nickname[], const reason[] = "");
// Up to 28 days; 0 removes the timeout.
native DBR_SetGuildMemberTimeout(DiscordGuild:guild, DiscordUser:user, seconds, const reason[] = "");
native DBR_SetGuildMemberMute(DiscordGuild:guild, DiscordUser:user, bool:mute, const reason[] = "");
native DBR_SetGuildMemberDeaf(DiscordGuild:guild, DiscordUser:user, bool:deaf, const reason[] = "");
// DISCORD_INVALID_CHANNEL disconnects the member from voice.
native DBR_SetGuildMemberVoiceChannel(DiscordGuild:guild, DiscordUser:user, DiscordChannel:channel, const reason[] = "");
native DBR_AddGuildMemberRole(DiscordGuild:guild, DiscordUser:user, DiscordRole:role, const reason[] = "");
native DBR_RemoveGuildMemberRole(DiscordGuild:guild, DiscordUser:user, DiscordRole:role, const reason[] = "");

stock DBR_ClearGuildMemberNickname(DiscordGuild:guild, DiscordUser:user, const reason[] = "")
{
	return DBR_SetGuildMemberNickname(guild, user, "", reason);
}

stock DBR_RemoveGuildMemberTimeout(DiscordGuild:guild, DiscordUser:user, const reason[] = "")
{
	return DBR_SetGuildMemberTimeout(guild, user, 0, reason);
}

stock DBR_DisconnectGuildMemberVoice(DiscordGuild:guild, DiscordUser:user, const reason[] = "")
{
	return DBR_SetGuildMemberVoiceChannel(guild, user, DISCORD_INVALID_CHANNEL, reason);
}

// -----------------------------------------------------------------------------
// Roles
// -----------------------------------------------------------------------------

native DiscordRole:DBR_FindRoleByID(const role_id[]);
native DiscordRole:DBR_FindRoleByName(DiscordGuild:guild, const role_name[]);
// callback(DiscordRole:role, ...)
native DBR_FetchRole(DiscordGuild:guild, DiscordRole:role, const callback[], const format[] = "", {Float, _}:...);
native DBR_GetRoleID(DiscordRole:role, dest[DISCORD_ID_SIZE], max_size = sizeof dest);
native DBR_GetRoleName(DiscordRole:role, dest[], max_size = sizeof dest);
native DBR_GetRoleColour(DiscordRole:role, &colour);
native DBR_GetRoleColor(DiscordRole:role, &color) = DBR_GetRoleColour;
native DBR_GetRolePermissions(DiscordRole:role, &perm_high, &perm_low);
native DBR_IsRoleHoist(DiscordRole:role, &bool:is_hoist);
native DBR_GetRolePosition(DiscordRole:role, &position);
native DBR_IsRoleMentionable(DiscordRole:role, &bool:is_mentionable);
native DBR_IsRoleManaged(DiscordRole:role, &bool:is_managed);
native DBR_SetGuildRolePosition(DiscordGuild:guild, DiscordRole:role, position);
native DBR_SetGuildRoleName(DiscordGuild:guild, DiscordRole:role, const name[]);
native DBR_SetGuildRolePermissions(DiscordGuild:guild, DiscordRole:role, perm_high, perm_low);
native DBR_SetGuildRoleColour(DiscordGuild:guild, DiscordRole:role, colour);
native DBR_SetGuildRoleColor(DiscordGuild:guild, DiscordRole:role, color) = DBR_SetGuildRoleColour;
native DBR_SetGuildRoleHoist(DiscordGuild:guild, DiscordRole:role, bool:hoist);
native DBR_SetGuildRoleMentionable(DiscordGuild:guild, DiscordRole:role, bool:mentionable);
// callback(DiscordRole:role, ...)
native DBR_CreateGuildRole(DiscordGuild:guild, const name[], const callback[] = "", const format[] = "", {Float, _}:...);
native DBR_DeleteGuildRole(DiscordGuild:guild, DiscordRole:role);

// -----------------------------------------------------------------------------
// Messages
// -----------------------------------------------------------------------------

// callback(DiscordMessage:message, ...)
native DBR_FetchMessage(DiscordChannel:channel, const message_id[], const callback[], const format[] = "", {Float, _}:...);
native DBR_GetMessageID(DiscordMessage:message, dest[DISCORD_ID_SIZE], max_size = sizeof dest);
native DBR_GetMessageChannel(DiscordMessage:message, &DiscordChannel:channel);
native DBR_GetMessageAuthor(DiscordMessage:message, &DiscordUser:author);
native DBR_GetMessageContent(DiscordMessage:message, dest[], max_size = sizeof dest);
native DBR_IsMessageTTS(DiscordMessage:message, &bool:is_tts);
native DBR_IsMessageMentioningEveryone(DiscordMessage:message, &bool:mentions_everyone);
native DBR_GetMessageUserMentionCount(DiscordMessage:message, &mentioned_user_count);
native DBR_GetMessageUserMention(DiscordMessage:message, offset, &DiscordUser:mentioned_user);
native DBR_GetMessageRoleMentionCount(DiscordMessage:message, &mentioned_role_count);
native DBR_GetMessageRoleMention(DiscordMessage:message, offset, &DiscordRole:mentioned_role);
// Messages are only cached while their callback runs unless persistent.
native DBR_SetMessagePersistent(DiscordMessage:message, bool:persistent);
native DBR_ForgetMessage(DiscordMessage:message);
native DBR_EditMessage(DiscordMessage:message, const content[], DiscordEmbed:embed = DISCORD_INVALID_EMBED);
native DBR_EditMessageWithBuilder(DiscordMessage:message, DiscordMessageBuilder:builder);
// callback(DiscordMessage:reply, ...)
native DBR_ReplyMessage(DiscordMessage:message, const content[], bool:mention_author = true, const callback[] = "", const format[] = "", {Float, _}:...);
native DBR_DeleteMessage(DiscordMessage:message);
native DBR_DeleteMessageByID(DiscordChannel:channel, const message_id[], const reason[] = "");
// Deletes up to 100 recent messages younger than 14 days.  callback(deleted_count, ...)
native DBR_BulkDeleteMessages(DiscordChannel:channel, amount, const callback[] = "", const format[] = "", {Float, _}:...);
native DBR_PinMessage(DiscordMessage:message);
native DBR_UnpinMessage(DiscordMessage:message);
native DBR_CrosspostMessage(DiscordMessage:message);
native DBR_EscapeMarkdown(const src[], dest[], max_size = sizeof dest);

native DiscordEmoji:DBR_CreateEmoji(const name[DISCORD_EMOJI_NAME_SIZE], const snowflake[DISCORD_ID_SIZE] = "");
native DBR_DeleteEmoji(DiscordEmoji:emoji);
native DBR_GetEmojiName(DiscordEmoji:emoji, dest[DISCORD_EMOJI_NAME_SIZE], max_size = sizeof dest);
// Consumes the emoji handle.
native DBR_CreateReaction(DiscordMessage:message, DiscordEmoji:reaction_emoji);
native DBR_DeleteMessageReaction(DiscordMessage:message, DiscordEmoji:reaction_emoji = DiscordEmoji:0);

// -----------------------------------------------------------------------------
// Embeds
// -----------------------------------------------------------------------------

native DiscordEmbed:DBR_CreateEmbed(const title[] = "", const description[] = "", const url[] = "", const timestamp[] = "", colour = 0, const footer_text[] = "", const footer_icon_url[] = "", const thumbnail_url[] = "", const image_url[] = "");
native DBR_DeleteEmbed(DiscordEmbed:embed);
native DBR_AddEmbedField(DiscordEmbed:embed, const name[], const value[], bool:inline = false);
native DBR_ClearEmbedFields(DiscordEmbed:embed);
native DBR_SetEmbedTitle(DiscordEmbed:embed, const title[]);
native DBR_SetEmbedDescription(DiscordEmbed:embed, const description[]);
native DBR_SetEmbedURL(DiscordEmbed:embed, const url[]);
// ISO 8601, for example "2026-01-31T18:00:00Z".
native DBR_SetEmbedTimestamp(DiscordEmbed:embed, const timestamp[]);
native DBR_SetEmbedColour(DiscordEmbed:embed, colour);
native DBR_SetEmbedColor(DiscordEmbed:embed, color) = DBR_SetEmbedColour;
native DBR_SetEmbedAuthor(DiscordEmbed:embed, const name[], const url[] = "", const icon_url[] = "");
native DBR_SetEmbedFooter(DiscordEmbed:embed, const footer_text[], const footer_icon_url[] = "");
native DBR_SetEmbedThumbnail(DiscordEmbed:embed, const thumbnail_url[]);
native DBR_SetEmbedImage(DiscordEmbed:embed, const image_url[]);
// Consumes the embed.  callback(DiscordMessage:message, ...)
native DBR_SendChannelEmbedMessage(DiscordChannel:channel, DiscordEmbed:embed, const message[] = "", const callback[] = "", const format[] = "", {Float, _}:...);

// -----------------------------------------------------------------------------
// Message builders
//
// A builder combines content, up to 10 embeds and components.  When any
// Components V2 layout component (container, section, text display, media
// gallery, separator, thumbnail) is added, the message is sent as Components
// V2: content becomes a leading text display and embeds are not allowed.
// -----------------------------------------------------------------------------

native DiscordMessageBuilder:DBR_CreateMessageBuilder(const content[] = "");
native DBR_DestroyMessageBuilder(DiscordMessageBuilder:builder);
native DBR_SetBuilderContent(DiscordMessageBuilder:builder, const content[]);
// Consumes the embed.
native DBR_AddBuilderEmbed(DiscordMessageBuilder:builder, DiscordEmbed:embed);
// Top-level components are action rows or Components V2 layout components.
native DBR_AddBuilderComponent(DiscordMessageBuilder:builder, DiscordComponent:component);
native DBR_SetBuilderTTS(DiscordMessageBuilder:builder, bool:tts);
native DBR_SetBuilderSilent(DiscordMessageBuilder:builder, bool:silent);
native DBR_SetBuilderSuppressEmbeds(DiscordMessageBuilder:builder, bool:suppress);
native DBR_SetBuilderReply(DiscordMessageBuilder:builder, DiscordMessage:message, bool:mention_author = true);
native DBR_SetBuilderMentions(DiscordMessageBuilder:builder, bool:users = true, bool:roles = true, bool:everyone = false);
// Consumes the builder.  callback(DiscordMessage:message, ...)
native DBR_SendMessage(DiscordChannel:channel, DiscordMessageBuilder:builder, const callback[] = "", const format[] = "", {Float, _}:...);
native DBR_SendDirectMessage(DiscordUser:user, DiscordMessageBuilder:builder, const callback[] = "", const format[] = "", {Float, _}:...);

// -----------------------------------------------------------------------------
// Components
// -----------------------------------------------------------------------------

native DiscordComponent:DBR_CreateActionRow();
// Link buttons take a URL and premium buttons a SKU id instead of a custom id.
// Emoji: "🔥", "<:name:id>", "<a:name:id>" or "name:id".
native DiscordComponent:DBR_CreateButton(DiscordButtonStyle:style, const label[], const custom_id_or_url[] = "", const emoji[] = "", bool:disabled = false);
native DiscordComponent:DBR_CreateSelectMenu(DiscordSelectMenuType:type, const custom_id[], const placeholder[] = "", min_values = 1, max_values = 1, bool:disabled = false);
native DBR_AddSelectMenuOption(DiscordComponent:menu, const label[], const value[], const description[] = "", const emoji[] = "", bool:is_default = false);
native DBR_AddSelectMenuDefault(DiscordComponent:menu, const id[], DiscordSelectDefaultType:type);
native DBR_AddSelectMenuChannelType(DiscordComponent:menu, DiscordChannelType:type);
native DBR_SetSelectMenuRequired(DiscordComponent:menu, bool:required);
native DiscordComponent:DBR_CreateTextInput(const custom_id[], DiscordTextInputStyle:style = DISCORD_TEXT_INPUT_SHORT, const placeholder[] = "", const value[] = "", min_length = 0, max_length = 4000, bool:required = true);
native DiscordComponent:DBR_CreateFileUpload(const custom_id[], min_files = 1, max_files = 1, bool:required = true);
native DiscordComponent:DBR_CreateTextDisplay(const content[]);
native DiscordComponent:DBR_CreateSeparator(bool:divider = true, DiscordSeparatorSpacing:spacing = DISCORD_SPACING_SMALL);
// An accent colour of -1 leaves the container without an accent bar.
native DiscordComponent:DBR_CreateContainer(accent_colour = -1, bool:spoiler = false);
native DiscordComponent:DBR_CreateSection();
native DiscordComponent:DBR_CreateThumbnail(const url[], const description[] = "", bool:spoiler = false);
native DiscordComponent:DBR_CreateMediaGallery();
native DBR_AddMediaGalleryItem(DiscordComponent:gallery, const url[], const description[] = "", bool:spoiler = false);
// Wraps a text input, select menu or file upload with a label for modals.
native DiscordComponent:DBR_CreateLabel(const label[], DiscordComponent:component, const description[] = "");
native DBR_AddComponent(DiscordComponent:parent, DiscordComponent:child);
// The accessory of a section is a button or a thumbnail.
native DBR_SetSectionAccessory(DiscordComponent:section, DiscordComponent:accessory);
native DBR_SetComponentID(DiscordComponent:component, id);
native DBR_SetComponentDisabled(DiscordComponent:component, bool:disabled);
// Only components that are not attached to a parent can be destroyed.
native DBR_DestroyComponent(DiscordComponent:component);

// -----------------------------------------------------------------------------
// Modals
// -----------------------------------------------------------------------------

native DiscordModal:DBR_CreateModal(const custom_id[], const title[]);
native DBR_DestroyModal(DiscordModal:modal);
// Labels, text displays or action rows holding a text input; up to 5.
native DBR_AddModalComponent(DiscordModal:modal, DiscordComponent:component);
native DBR_AddModalTextInput(DiscordModal:modal, const custom_id[], const label[], DiscordTextInputStyle:style = DISCORD_TEXT_INPUT_SHORT, const placeholder[] = "", const value[] = "", min_length = 0, max_length = 4000, bool:required = true, const description[] = "");

// -----------------------------------------------------------------------------
// Application commands
//
// Commands are published automatically a couple of seconds after the last
// change once the bot is ready.  Each scope (global or one guild) is replaced
// as a whole, so commands created outside this plugin in the same scope are
// removed.
// -----------------------------------------------------------------------------

// callback(DiscordInteraction:interaction, DiscordUser:user)
native DiscordCommand:DBR_CreateCommand(const name[], const description[] = "", DiscordGuild:guild = DISCORD_INVALID_GUILD, const callback[] = "", DiscordCommandType:type = DISCORD_COMMAND_SLASH);
native DBR_DestroyCommand(DiscordCommand:command);
native DiscordCommandOption:DBR_AddCommandOption(DiscordCommand:command, DiscordCommandOptionType:type, const name[], const description[], bool:required = false, DiscordCommandOption:parent = DISCORD_INVALID_COMMAND_OPTION);
native DBR_AddOptionChoice(DiscordCommandOption:option, const name[], const value[]);
native DBR_AddOptionChoiceInt(DiscordCommandOption:option, const name[], value);
native DBR_AddOptionChoiceFloat(DiscordCommandOption:option, const name[], Float:value);
native DBR_SetOptionAutocomplete(DiscordCommandOption:option, bool:enabled = true);
native DBR_SetOptionRange(DiscordCommandOption:option, Float:min_value, Float:max_value);
native DBR_SetOptionLength(DiscordCommandOption:option, min_length, max_length);
native DBR_AddOptionChannelType(DiscordCommandOption:option, DiscordChannelType:type);
// DISCORD_COMMAND_EVERYONE, DISCORD_COMMAND_ADMINISTRATORS or a permission bitfield as a decimal string.
native DBR_SetCommandPermissions(DiscordCommand:command, const permissions[]);
native DBR_SetCommandNSFW(DiscordCommand:command, bool:nsfw);
native DBR_SetCommandContexts(DiscordCommand:command, bool:guild = true, bool:bot_dm = false, bool:private_channel = false);
native DBR_DeployCommands();
native DBR_SetCommandAutoDeploy(bool:enabled);

// -----------------------------------------------------------------------------
// Interactions
//
// Dispatch order: DBR_OnInteraction (return 0 to stop), then the command
// callback or a handler registered with DBR_RegisterHandler, and
// otherwise DBR_OnCommand / DBR_OnButton / DBR_OnSelectMenu /
// DBR_OnModalSubmit / DBR_OnAutocomplete.  An interaction nobody
// answered is deferred automatically after the callbacks return.  Handles stay
// valid for 15 minutes, so a deferred interaction can be answered later.
// -----------------------------------------------------------------------------

native DiscordInteractionType:DBR_GetInteractionType(DiscordInteraction:interaction);
native DBR_GetInteractionID(DiscordInteraction:interaction, dest[DISCORD_ID_SIZE], max_size = sizeof dest);
native DBR_GetInteractionUser(DiscordInteraction:interaction, &DiscordUser:user);
native DBR_GetInteractionChannel(DiscordInteraction:interaction, &DiscordChannel:channel);
native DBR_GetInteractionGuild(DiscordInteraction:interaction, &DiscordGuild:guild);
native DBR_GetInteractionMessage(DiscordInteraction:interaction, &DiscordMessage:message);
native DBR_GetInteractionLocale(DiscordInteraction:interaction, dest[], max_size = sizeof dest);
native bool:DBR_HasInteractionPermission(DiscordInteraction:interaction, DiscordPermission:permission);
native DBR_GetInteractionData(DiscordInteraction:interaction, dest[], max_size = sizeof dest);
native bool:DBR_IsInteractionResponded(DiscordInteraction:interaction);

native DBR_GetInteractionCommandName(DiscordInteraction:interaction, dest[DISCORD_COMMAND_NAME_SIZE], max_size = sizeof dest);
native DiscordCommandType:DBR_GetInteractionCommandType(DiscordInteraction:interaction);
native DBR_GetInteractionSubcommand(DiscordInteraction:interaction, dest[DISCORD_COMMAND_NAME_SIZE], max_size = sizeof dest);
native DBR_GetInteractionSubGroup(DiscordInteraction:interaction, dest[DISCORD_COMMAND_NAME_SIZE], max_size = sizeof dest);
// User or message targeted by a context menu command.
native DBR_GetInteractionTargetID(DiscordInteraction:interaction, dest[DISCORD_ID_SIZE], max_size = sizeof dest);
native bool:DBR_HasInteractionOption(DiscordInteraction:interaction, const name[]);
native DiscordCommandOptionType:DBR_GetInteractionOptionType(DiscordInteraction:interaction, const name[]);
// User mentions in string command options, including nested subcommands.
// Accepts <@id> and <@!id>; preserves order and duplicates. Role/channel mentions
// and typed user options are excluded. Valid interactions with no mentions count 0.
native DBR_GetInteractionMentionCount(DiscordInteraction:interaction, &mentioned_user_count);
// Zero-based offset. Returns a user handle even if that user is not cached.
// Invalid/expired interactions, offsets or output references return 0.
native DBR_GetInteractionMention(DiscordInteraction:interaction, offset, &DiscordUser:mentioned_user);
native DBR_GetInteractionOptionString(DiscordInteraction:interaction, const name[], dest[], max_size = sizeof dest);
native DBR_GetInteractionOptionInt(DiscordInteraction:interaction, const name[], &value);
native DBR_GetInteractionOptionFloat(DiscordInteraction:interaction, const name[], &Float:value);
native DBR_GetInteractionOptionBool(DiscordInteraction:interaction, const name[], &bool:value);
native DBR_GetInteractionOptionUser(DiscordInteraction:interaction, const name[], &DiscordUser:user);
native DBR_GetInteractionOptionChannel(DiscordInteraction:interaction, const name[], &DiscordChannel:channel);
native DBR_GetInteractionOptionRole(DiscordInteraction:interaction, const name[], &DiscordRole:role);
native DBR_GetInteractionFocusedOption(DiscordInteraction:interaction, dest[DISCORD_COMMAND_NAME_SIZE], max_size = sizeof dest);
native DBR_GetInteractionAttachmentURL(DiscordInteraction:interaction, const attachment_id[], dest[], max_size = sizeof dest);

native DBR_GetInteractionCustomID(DiscordInteraction:interaction, dest[DISCORD_CUSTOM_ID_SIZE], max_size = sizeof dest);
native DiscordComponentType:DBR_GetInteractionComponentType(DiscordInteraction:interaction);
// Select menus: leave custom_id empty.  Modals: pass the input's custom id.
native DBR_GetInteractionValueCount(DiscordInteraction:interaction, const custom_id[] = "");
native DBR_GetInteractionValue(DiscordInteraction:interaction, const custom_id[], dest[], max_size = sizeof dest, index = 0);

native DBR_DeferInteraction(DiscordInteraction:interaction, bool:ephemeral = false);
// Component and modal interactions: acknowledge without a visible reply.
native DBR_DeferInteractionUpdate(DiscordInteraction:interaction);
// Replies, edits the deferred reply or sends a follow-up, whichever fits.
native DBR_RespondInteraction(DiscordInteraction:interaction, const content[], bool:ephemeral = false);
native DBR_RespondInteractionEmbed(DiscordInteraction:interaction, DiscordEmbed:embed, const content[] = "", bool:ephemeral = false);
native DBR_RespondInteractionMessage(DiscordInteraction:interaction, DiscordMessageBuilder:builder, bool:ephemeral = false);
// Component and modal interactions: edit the message the component belongs to.
native DBR_UpdateInteractionMessage(DiscordInteraction:interaction, DiscordMessageBuilder:builder);
native DBR_EditInteractionResponse(DiscordInteraction:interaction, DiscordMessageBuilder:builder);
native DBR_DeleteInteractionResponse(DiscordInteraction:interaction);
native DBR_SendInteractionFollowup(DiscordInteraction:interaction, DiscordMessageBuilder:builder, bool:ephemeral = false);
// Command and component interactions only, as the first response.
native DBR_ShowModal(DiscordInteraction:interaction, DiscordModal:modal);
// Autocomplete choices are sent when the callback returns, or with DBR_SendAutocomplete.
native DBR_AddAutocompleteChoice(DiscordInteraction:interaction, const name[], const value[]);
native DBR_AddAutocompleteChoiceInt(DiscordInteraction:interaction, const name[], value);
native DBR_AddAutocompleteChoiceFloat(DiscordInteraction:interaction, const name[], Float:value);
native DBR_SendAutocomplete(DiscordInteraction:interaction);
native DBR_SetInteractionAutoDefer(bool:enabled);

// callback(DiscordInteraction:interaction, DiscordUser:user, const key[])
// `key` is the command name for commands and autocomplete, the custom id otherwise.
native DBR_RegisterHandler(DiscordInteractionType:type, const key[], const callback[], bool:match_prefix = false);
native DBR_UnregisterHandler(DiscordInteractionType:type, const key[]);

// -----------------------------------------------------------------------------
// Bot
// -----------------------------------------------------------------------------

native DiscordUser:DBR_GetBotUser();
native DiscordBotPresenceStatus:DBR_GetBotPresenceStatus();
native DBR_SetBotPresenceStatus(DiscordBotPresenceStatus:status);
// Streaming activities use `url`; custom activities show `name` as the status text.
native DBR_SetBotActivity(const name[], DiscordActivityType:type = DISCORD_ACTIVITY_PLAYING, const url[] = "");
native DBR_SetBotNickname(DiscordGuild:guild, const nickname[]);
native DBR_SetBotUsername(const username[]);
// A png/jpg/gif/webp path (also looked up in scriptfiles/) or a data URI; "" removes it.
native DBR_SetBotAvatar(const image[]);
native DBR_SetBotBanner(const image[]);
native DBR_SetBotDescription(const description[]);

stock DBR_ClearBotActivity()
{
	return DBR_SetBotActivity("");
}

stock DBR_ClearBotNickname(DiscordGuild:guild)
{
	return DBR_SetBotNickname(guild, "");
}

// -----------------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------------

forward DBR_OnReady();
forward DBR_OnDisconnected();
forward DBR_OnActionFail(const action[], http_status, error_code, const message[]);

forward DBR_OnInteraction(DiscordInteraction:interaction, DiscordUser:user, DiscordInteractionType:type);
forward DBR_OnCommand(DiscordInteraction:interaction, DiscordUser:user, const command[]);
forward DBR_OnButton(DiscordInteraction:interaction, DiscordUser:user, const custom_id[]);
forward DBR_OnSelectMenu(DiscordInteraction:interaction, DiscordUser:user, const custom_id[]);
forward DBR_OnModalSubmit(DiscordInteraction:interaction, DiscordUser:user, const custom_id[]);
forward DBR_OnAutocomplete(DiscordInteraction:interaction, DiscordUser:user, const command[], const option[]);

forward DBR_OnChannelCreate(DiscordChannel:channel);
forward DBR_OnChannelUpdate(DiscordChannel:channel);
forward DBR_OnChannelDelete(DiscordChannel:channel);
forward DBR_OnMessageCreate(DiscordMessage:message);
forward DBR_OnMessageUpdate(DiscordMessage:message);
forward DBR_OnMessageDelete(DiscordMessage:message);
forward DBR_OnMessageReaction(DiscordMessage:message, DiscordUser:reaction_user, DiscordEmoji:emoji, DiscordMessageReactionType:reaction_type);
forward DBR_OnUserUpdate(DiscordUser:user);
forward DBR_OnGuildCreate(DiscordGuild:guild);
forward DBR_OnGuildUpdate(DiscordGuild:guild);
forward DBR_OnGuildDelete(DiscordGuild:guild);
forward DBR_OnGuildMemberAdd(DiscordGuild:guild, DiscordUser:user);
forward DBR_OnGuildMemberUpdate(DiscordGuild:guild, DiscordUser:user);
forward DBR_OnGuildMemberVoiceUpdate(DiscordGuild:guild, DiscordUser:user, DiscordChannel:channel);
forward DBR_OnGuildMemberRemove(DiscordGuild:guild, DiscordUser:user);
forward DBR_OnGuildRoleCreate(DiscordGuild:guild, DiscordRole:role);
forward DBR_OnGuildRoleUpdate(DiscordGuild:guild, DiscordRole:role);
forward DBR_OnGuildRoleDelete(DiscordGuild:guild, DiscordRole:role);
