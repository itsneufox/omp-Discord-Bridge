/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#pragma once

// Helpers shared by the native implementation files.  Everything here runs on
// the server thread; REST work is handed to DiscordBot::submitRestTask and its
// results come back through DiscordBot::enqueueCompletion.

#include "natives.hpp"
#include "discord-builders.hpp"
#include "discord-http.hpp"
#include <amx/amx.h>
#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class DiscordBot;
class DiscordBridgeComponent;
class DiscordChannel;
class DiscordGuild;
class DiscordMessage;
class DiscordRole;
class DiscordUser;

namespace DiscordNatives
{
using NativePawnScript = IPawnScript;
using NativePawnComponent = IPawnComponent;

struct PawnCallbackArg
{
	enum class Type { Cell, String, Array, Reference };
	Type type = Type::Cell;
	cell value = 0;
	cell referenceAddress = 0;
	std::string text;
	std::vector<cell> array;
};

// Legacy DCC result getters are scoped to the requesting script and callback.
enum class DccResult { None, Message, GuildChannel, GuildRole, PrivateChannel };

struct PreparedPawnCallback
{
	DccResult dccResult = DccResult::None;
	int scriptId = -1;
	std::string name;
	std::vector<PawnCallbackArg> args;
};

extern std::unordered_map<cell, DiscordBuilders::Embed> g_embeds;

DiscordBridgeComponent* component();
DiscordBot* nativeBot();
NativePawnScript* pawnScriptFor(AMX* amx);
NativePawnScript* pawnScriptForId(int scriptId);
NativePawnScript* findPawnScriptWithPublic(const char* name, NativePawnScript* preferred = nullptr);

// Returns the cached guild, creating it from its ID when the gateway has
// not cached it, so that member data has somewhere to live.
DiscordGuild* ensureGuildCached(const std::string& guildId);

// Reads a script string converted to UTF-8 for Discord.
std::string getAmxString(AMX* amx, cell amxParam);
// Reads a script string exactly as the script stores it.
std::string getAmxStringRaw(AMX* amx, cell amxParam);
std::string fromPawnText(const std::string& text);
// Converts Discord's UTF-8 to the script's encoding (see DBR_SetTextEncoding).
std::string toPawnText(const std::string& text);
bool setAmxString(AMX* amx, cell amxParam, const std::string& value, cell maxSize);
cell* nativeRef(AMX* amx, cell address);
bool isDiscordSnowflake(const std::string& value);

inline size_t nativeParamCount(const cell* params)
{
	return params[0] > 0 ? static_cast<size_t>(params[0]) / sizeof(cell) : 0;
}

cell assignChannelHandle(StringView channelId);
cell assignUserHandle(StringView userId);
cell assignGuildHandle(StringView guildId);
cell assignMessageHandle(StringView messageId);
cell assignRoleHandle(StringView roleId);

std::string channelIdForHandle(cell handle);
std::string userIdForHandle(cell handle);
std::string guildIdForHandle(cell handle);
std::string roleIdForHandle(cell handle);
// Resolves a message handle even when the message itself is no longer cached.
bool messageRefForHandle(cell handle, std::string& channelId, std::string& messageId);
void rememberMessageChannel(cell handle, StringView channelId);

DiscordChannel* resolveChannelByHandle(cell handle);
DiscordUser* resolveUserByHandle(cell handle);
DiscordGuild* resolveGuildByHandle(cell handle);
DiscordMessage* resolveMessageByHandle(cell handle);
DiscordRole* resolveRoleByHandle(cell handle);

// Captures `callback[], format[], {Float, _}:...` so it can run after a REST
// request completes.  An empty callback name is valid and captures nothing.
bool capturePawnCallback(AMX* amx, cell callbackParam, cell formatParam, cell* params, size_t firstParam,
	std::shared_ptr<PreparedPawnCallback>& prepared);
// `leading` values are passed before the captured arguments, so a callback
// receives the entity created by the request as its first parameter.
bool executePawnCallback(const PreparedPawnCallback& prepared, const std::vector<cell>& leading = {});

void logWarning(const std::string& message);
void logInfo(const std::string& message);

using RestRequest = std::function<DiscordHTTP::Response(DiscordHTTP&)>;
using RestCompletion = std::function<void(const DiscordHTTP::Response&)>;
// Queues a REST request on the bot's worker.  A failed request fires
// DBR_OnActionFail(action[], http_status, error_code, message[]); the
// completion then runs on the server thread whether the request failed or not.
bool submitAction(const std::string& action, RestRequest request, RestCompletion completion = {});

// Runs a message callback with the created message handle (0 on failure).
void completeMessageResponse(bool success, const std::string& responseBody,
	const std::shared_ptr<PreparedPawnCallback>& callback);
// Lets DBR_GetChannelGuild answer for channels that are not cached yet.
void rememberChannelGuild(cell channelHandle, StringView guildId);

template <typename... Args>
bool callPawnPublicOnScript(NativePawnScript* script, const char* name, cell& result, Args... args)
{
	if (!script) return false;
	int publicIndex = -1;
	if (script->FindPublic(name, &publicIndex) != AMX_ERR_NONE || publicIndex < 0 || publicIndex == INT_MAX)
	{
		return false;
	}
	const int error = script->CallChecked(publicIndex, result, args...);
	if (error != AMX_ERR_NONE) script->PrintError(error);
	return true;
}

// Calls the first loaded script exporting `name`: the main script first, then
// side scripts.  Returns the public's return value, or `fallback` when no
// script exports it.
template <typename... Args>
cell callPawnPublic(const char* name, cell fallback, Args... args)
{
	NativePawnScript* script = findPawnScriptWithPublic(name);
	cell result = fallback;
	if (!script || !callPawnPublicOnScript(script, name, result, args...)) return fallback;
	return result;
}

void appendCoreNatives(std::vector<AMX_NATIVE_INFO>& natives);
void appendInteractionNatives(std::vector<AMX_NATIVE_INFO>& natives);
void appendManagementNatives(std::vector<AMX_NATIVE_INFO>& natives);

void resetInteractionState();
void forgetInteractionScript(int scriptId);
void serviceInteractionState();
void onInteractionBotReady();
void onInteractionBotDisconnected();
}
