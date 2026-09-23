/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#include "natives.hpp"
#include "natives-internal.hpp"
#include "discord-bot.hpp"
#include "discord-component.hpp"
#include "discord-channel.hpp"
#include "discord-guild.hpp"
#include "discord-http.hpp"
#include "discord-log.hpp"
#include "discord-message.hpp"
#include "discord-role.hpp"
#include "discord-user.hpp"
#include "discord-json.hpp"
#include "utils.hpp"
#include <algorithm>
#include <array>
#include <utility>
#include <amx/amx.h>
#include <amx/amx2.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <cctype>
#include <cstdint>
#include <atomic>

namespace DiscordNatives
{
std::unordered_map<cell, std::string> g_channelHandleToId;
// Interaction payloads carry the channel's guild ID even before the gateway
// cache has received the corresponding channel object.
std::unordered_map<cell, std::string> g_channelHandleToGuildId;
std::unordered_map<std::string, cell> g_channelIdToHandle;
cell g_nextChannelHandle = 1;
std::unordered_map<cell, std::string> g_userHandleToId;
std::unordered_map<std::string, cell> g_userIdToHandle;
cell g_nextUserHandle = 1;
std::unordered_map<cell, std::string> g_guildHandleToId;
std::unordered_map<std::string, cell> g_guildIdToHandle;
cell g_nextGuildHandle = 1;
std::unordered_map<cell, std::string> g_messageHandleToId;
std::unordered_map<std::string, cell> g_messageIdToHandle;
cell g_nextMessageHandle = 1;
std::unordered_map<cell, std::string> g_roleHandleToId;
std::unordered_map<std::string, cell> g_roleIdToHandle;
cell g_nextRoleHandle = 1;
std::unordered_map<cell, std::string> g_emojiHandleToToken;
cell g_nextEmojiHandle = 1;

std::unordered_map<cell, std::string> g_messageHandleToChannelId;
std::unordered_map<cell, DiscordBuilders::Embed> g_embeds;
cell g_nextEmbedHandle = 1;

DiscordBridgeComponent* component()
{
	return DiscordBridgeComponent::getInstance();
}

NativePawnScript* pawnScriptFor(AMX* amx)
{
	DiscordBridgeComponent* bridge = component();
	NativePawnComponent* pawn = bridge ? bridge->getPawnComponent() : nullptr;
	return pawn ? pawn->getScript(amx) : nullptr;
}

int pawnGetAddr(AMX* amx, cell address, cell** physicalAddress)
{
	NativePawnScript* script = pawnScriptFor(amx);
	return script ? script->GetAddr(address, physicalAddress) : AMX_ERR_INIT;
}

int pawnPushAddress(NativePawnScript& script, cell* physicalAddress)
{
	cell amxAddress = 0;
	if (script.MakeAddr(physicalAddress, &amxAddress) != AMX_ERR_NONE)
	{
		return AMX_ERR_GENERAL;
	}
	return script.Push(amxAddress);
}

void resetNativeHandles()
{
	g_channelHandleToId.clear();
	g_channelHandleToGuildId.clear();
	g_channelIdToHandle.clear();
	g_nextChannelHandle = 1;

	g_userHandleToId.clear();
	g_userIdToHandle.clear();
	g_nextUserHandle = 1;

	g_guildHandleToId.clear();
	g_guildIdToHandle.clear();
	g_nextGuildHandle = 1;

	g_messageHandleToId.clear();
	g_messageIdToHandle.clear();
	g_nextMessageHandle = 1;

	g_roleHandleToId.clear();
	g_roleIdToHandle.clear();
	g_nextRoleHandle = 1;

	g_emojiHandleToToken.clear();
	g_nextEmojiHandle = 1;

	g_messageHandleToChannelId.clear();
	g_embeds.clear();
	g_nextEmbedHandle = 1;
	resetInteractionState();
}

cell assignChannelHandle(StringView channelId)
{
	const std::string id(channelId.data(), channelId.length());
	const auto it = g_channelIdToHandle.find(id);
	if (it != g_channelIdToHandle.end())
	{
		return it->second;
	}

	const cell handle = g_nextChannelHandle++;
	g_channelHandleToId.emplace(handle, id);
	g_channelIdToHandle.emplace(id, handle);
	return handle;
}

cell assignUserHandle(StringView userId)
{
	const std::string id(userId.data(), userId.length());
	const auto it = g_userIdToHandle.find(id);
	if (it != g_userIdToHandle.end())
	{
		return it->second;
	}

	const cell handle = g_nextUserHandle++;
	g_userHandleToId.emplace(handle, id);
	g_userIdToHandle.emplace(id, handle);
	return handle;
}

cell assignGuildHandle(StringView guildId)
{
	const std::string id(guildId.data(), guildId.length());
	const auto it = g_guildIdToHandle.find(id);
	if (it != g_guildIdToHandle.end())
	{
		return it->second;
	}

	const cell handle = g_nextGuildHandle++;
	g_guildHandleToId.emplace(handle, id);
	g_guildIdToHandle.emplace(id, handle);
	return handle;
}

cell assignMessageHandle(StringView messageId)
{
	const std::string id(messageId.data(), messageId.length());
	const auto it = g_messageIdToHandle.find(id);
	if (it != g_messageIdToHandle.end())
	{
		return it->second;
	}

	const cell handle = g_nextMessageHandle++;
	g_messageHandleToId.emplace(handle, id);
	g_messageIdToHandle.emplace(id, handle);
	return handle;
}

cell assignRoleHandle(StringView roleId)
{
	const std::string id(roleId.data(), roleId.length());
	const auto it = g_roleIdToHandle.find(id);
	if (it != g_roleIdToHandle.end())
	{
		return it->second;
	}

	const cell handle = g_nextRoleHandle++;
	g_roleHandleToId.emplace(handle, id);
	g_roleIdToHandle.emplace(id, handle);
	return handle;
}

cell assignEmojiHandle(const std::string& emojiToken)
{
	for (const auto& entry : g_emojiHandleToToken)
	{
		if (entry.second == emojiToken)
		{
			return entry.first;
		}
	}

	const cell handle = g_nextEmojiHandle++;
	g_emojiHandleToToken.emplace(handle, emojiToken);
	return handle;
}

std::string getAmxStringRaw(AMX* amx, cell amxParam)
{
	NativePawnScript* script = pawnScriptFor(amx);
	if (!script)
	{
		return {};
	}

	cell* addr = nullptr;
	if (script->GetAddr(amxParam, &addr) != AMX_ERR_NONE || !addr)
	{
		return {};
	}

	int len = 0;
	if (script->StrLen(addr, &len) != AMX_ERR_NONE || len < 0)
	{
		return {};
	}

	std::vector<char> buffer(static_cast<size_t>(len) + 1, '\0');
	if (script->GetString(buffer.data(), addr, false, buffer.size()) != AMX_ERR_NONE)
	{
		return {};
	}

	return std::string(buffer.data());
}

bool setAmxString(AMX* amx, cell amxParam, const std::string& value, size_t maxSize)
{
	NativePawnScript* script = pawnScriptFor(amx);
	if (!script)
	{
		return false;
	}

	cell* addr = nullptr;
	if (script->GetAddr(amxParam, &addr) != AMX_ERR_NONE || !addr)
	{
		return false;
	}

	const std::string converted = toPawnText(value);
	return script->SetString(addr, StringView(converted), false, false, maxSize) == AMX_ERR_NONE;
}

bool setAmxString(AMX* amx, cell amxParam, const std::string& value, cell maxSize)
{
	if (maxSize < 0) return false;
	return setAmxString(amx, amxParam, value, static_cast<size_t>(maxSize));
}

bool pawnScriptHasPublic(NativePawnScript* script, const char* name)
{
	if (!script) return false;
	int publicIndex = -1;
	return script->FindPublic(name, &publicIndex) == AMX_ERR_NONE &&
		publicIndex >= 0 && publicIndex != INT_MAX;
}

NativePawnScript* findPawnScriptWithPublic(const char* name, NativePawnScript* preferred)
{
	DiscordBridgeComponent* bridge = component();
	if (!bridge || !bridge->getPawnComponent()) return nullptr;
	NativePawnComponent* pawn = bridge->getPawnComponent();

	// Do not dereference a cached script pointer until it has been verified
	// against the current script set. This keeps command registrations safe
	// when a filterscript is unloaded and another one reuses its address.
	if (preferred)
	{
		if (pawn->mainScript() == preferred && pawnScriptHasPublic(preferred, name)) return preferred;
		for (NativePawnScript* script : pawn->sideScripts())
		{
			if (script == preferred && pawnScriptHasPublic(preferred, name)) return preferred;
		}
	}

	// Stop at the first AMX that actually exports the public.  Calling every
	// script would duplicate callbacks when a side script happens to contain
	// a public with the same name.
	NativePawnScript* main = pawn->mainScript();
	if (pawnScriptHasPublic(main, name)) return main;
	for (NativePawnScript* script : pawn->sideScripts())
	{
		if (pawnScriptHasPublic(script, name)) return script;
	}
	return nullptr;
}

DiscordGuild* ensureGuildCached(const std::string& guildId)
{
	DiscordBridgeComponent* bridge = component();
	if (!bridge || guildId.empty()) return nullptr;
	if (auto* cached = static_cast<DiscordGuild*>(bridge->findGuildById(guildId))) return cached;
	// Members only exist inside their guild.  Without the guild in the cache
	// (no GUILDS intent, or GUILD_CREATE has not arrived yet) freshly fetched
	// member data would be dropped and every getter would answer with empty
	// values, so create the guild from its ID and let the data land in it.
	return bridge->upsertGuildFromJson(DiscordJson { { "id", guildId } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), false);
}

void logWarning(const std::string& message)
{
	DiscordBridgeComponent* bridge = component();
	DiscordLogWarning(bridge ? bridge->getCore() : nullptr, message);
}

// Pawn scripts hold Windows-1252 text unless DBR_SetTextEncoding selects UTF-8;
// Discord always uses UTF-8.
constexpr cell TEXT_ENCODING_ANSI = 0;
constexpr cell TEXT_ENCODING_UTF8 = 1;
cell g_textEncoding = TEXT_ENCODING_ANSI;

std::string fromPawnText(const std::string& text)
{
	// Valid UTF-8 passes through, so UTF-8 scripts also work in ANSI mode.
	return DiscordUtils::isValidUtf8(text) ? text : DiscordUtils::windows1252ToUtf8(text);
}

std::string toPawnText(const std::string& text)
{
	return g_textEncoding == TEXT_ENCODING_UTF8 ? text : DiscordUtils::utf8ToWindows1252(text);
}

std::string getAmxString(AMX* amx, cell amxParam)
{
	return fromPawnText(getAmxStringRaw(amx, amxParam));
}

// Informational messages are only printed in debug mode; warnings always are.
bool g_debugMode = false;
bool g_connectIgnoredWarned = false;

void logInfo(const std::string& message)
{
	if (!g_debugMode) return;
	DiscordBridgeComponent* bridge = component();
	DiscordLogMessage(bridge ? bridge->getCore() : nullptr, message);
}

void reportActionFailure(const std::string& action, const DiscordHTTP::Response& response)
{
	std::string message = response.body.empty() ? "network or TLS failure" : response.body;
	int code = 0;
	const DiscordJson error = DiscordJson::parse(response.body, nullptr, false);
	if (error.is_object())
	{
		const auto text = error.find("message");
		if (text != error.end() && text->is_string()) message = text->get<std::string>();
		const auto number = error.find("code");
		if (number != error.end() && number->is_number_integer()) code = number->get<int>();
	}
	for (char& character : message)
	{
		if (character == '\r' || character == '\n') character = ' ';
	}
	if (message.size() > 256) message.resize(256);
	callPawnPublic("DBR_OnActionFail", 1, StringView(action), static_cast<cell>(response.statusCode),
		static_cast<cell>(code), StringView(toPawnText(message)));
}

bool submitAction(const std::string& action, RestRequest request, RestCompletion completion)
{
	DiscordBot* bot = nativeBot();
	if (!bot || !request) return false;
	return bot->submitRestTask([bot, action, request = std::move(request), completion = std::move(completion)](DiscordHTTP& http)
	{
		const DiscordHTTP::Response response = request(http);
		bot->enqueueCompletion([action, response, completion]()
		{
			if (!response.success) reportActionFailure(action, response);
			if (completion) completion(response);
		});
	});
}

constexpr size_t MAX_CALLBACK_ARRAY_CELLS = 4096;

NativePawnScript* pawnScriptForId(int scriptId)
{
	DiscordBridgeComponent* bridge = component();
	NativePawnComponent* pawn = bridge ? bridge->getPawnComponent() : nullptr;
	if (!pawn) return nullptr;

	NativePawnScript* main = pawn->mainScript();
	if (main && main->IsLoaded() && main->GetID() == scriptId) return main;
	for (NativePawnScript* script : pawn->sideScripts())
	{
		if (script && script->IsLoaded() && script->GetID() == scriptId) return script;
	}
	return nullptr;
}

bool pawnArrayRangeValid(NativePawnScript& script, cell address, size_t cells)
{
	if (address < 0 || cells == 0 || cells > MAX_CALLBACK_ARRAY_CELLS) return false;
	const uint64_t end = static_cast<uint64_t>(static_cast<uint32_t>(address)) + cells;
	return end <= static_cast<uint64_t>(script.GetSTP());
}

namespace
{
DccResult g_capturingDccResult = DccResult::None;
struct DccCallbackContext
{
	int scriptId = -1;
	DccResult kind = DccResult::None;
	cell handle = 0;
};
DccCallbackContext g_dccCallback;

// Restores the previous value even if an AMX/native call throws or nests.
template <typename T> struct ScopedDccValue
{
	T& target;
	T previous;
	ScopedDccValue(T& target, T value) : target(target), previous(target) { target = value; }
	~ScopedDccValue() { target = previous; }
};
}

bool executePawnCallback(const PreparedPawnCallback& prepared, const std::vector<cell>& leading)
{
	if (prepared.name.empty()) return true;
	NativePawnScript* script = pawnScriptForId(prepared.scriptId);
	if (!script) return false;

	int publicIndex = -1;
	if (script->FindPublic(prepared.name.c_str(), &publicIndex) != AMX_ERR_NONE || publicIndex < 0)
	{
		return false;
	}

	const cell heap = script->GetHEA();
	for (auto it = prepared.args.rbegin(); it != prepared.args.rend(); ++it)
	{
		int error = AMX_ERR_NONE;
		switch (it->type)
		{
			case PawnCallbackArg::Type::Cell:
				error = script->Push(it->value);
				break;
			case PawnCallbackArg::Type::String:
				error = script->PushString(nullptr, nullptr, StringView(it->text), false, false);
				break;
			case PawnCallbackArg::Type::Array:
				error = script->PushArray(nullptr, nullptr, it->array.data(), static_cast<int>(it->array.size()));
				break;
			case PawnCallbackArg::Type::Reference:
			{
				cell* reference = nullptr;
				if (script->GetAddr(it->referenceAddress, &reference) != AMX_ERR_NONE || !reference)
				{
					error = AMX_ERR_MEMACCESS;
				}
				else
				{
					error = pawnPushAddress(*script, reference);
				}
				break;
			}
		}
		if (error != AMX_ERR_NONE)
		{
			script->Release(heap);
			return false;
		}
	}
	for (auto it = leading.rbegin(); prepared.dccResult == DccResult::None && it != leading.rend(); ++it)
	{
		if (script->Push(*it) != AMX_ERR_NONE)
		{
			script->Release(heap);
			return false;
		}
	}

	if (g_debugMode)
	{
		// The public must declare the callback's own leading parameters before the
		// format arguments; a missing one shifts every later parameter, which is
		// why an integer then arrives as a string address.
		std::string call = prepared.name + "(";
		for (size_t i = 0; i < leading.size(); ++i)
		{
			call += (i ? ", " : "") + std::to_string(leading[i]);
		}
		for (size_t i = 0; i < prepared.args.size(); ++i)
		{
			call += (i || !leading.empty()) ? ", " : "";
			const PawnCallbackArg& arg = prepared.args[i];
			switch (arg.type)
			{
				case PawnCallbackArg::Type::String: call += "\"" + arg.text + "\""; break;
				case PawnCallbackArg::Type::Array: call += "[" + std::to_string(arg.array.size()) + " cells]"; break;
				case PawnCallbackArg::Type::Reference: call += "&" + std::to_string(arg.referenceAddress); break;
				default: call += std::to_string(arg.value); break;
			}
		}
		logInfo("[DiscordBridge] calling " + call + ") with " + std::to_string(leading.size())
			+ " leading parameter(s) before the format arguments");
	}

	ScopedDccValue<DccCallbackContext> context(g_dccCallback,
		{ prepared.scriptId, prepared.dccResult, leading.empty() ? 0 : leading.front() });
	cell result = 0;
	const int error = script->Exec(&result, publicIndex);
	script->Release(heap);
	return error == AMX_ERR_NONE;
}

// Pawn passes every variadic argument by reference: params[] holds the address
// of the value, so a number has to be read through it.  Strings and arrays are
// already addresses of the data itself.
bool readVariadicNumber(AMX* amx, cell address, cell& value)
{
	cell* reference = nullptr;
	if (pawnGetAddr(amx, address, &reference) != AMX_ERR_NONE || !reference) return false;
	value = *reference;
	return true;
}

bool callbackParametersValid(AMX* amx, cell callbackParam, cell formatParam, cell* params, size_t firstParam)
{
	const std::string callback = getAmxStringRaw(amx, callbackParam);
	// An empty callback is valid; its format and variadic arguments are then
	// ignored.
	if (callback.empty()) return true;
	if (callback.size() > 31) return false;

	DiscordBridgeComponent* bridge = component();
	NativePawnComponent* pawn = bridge ? bridge->getPawnComponent() : nullptr;
	NativePawnScript* script = pawn ? pawn->getScript(amx) : nullptr;
	if (!script) return false;
	int publicIndex = -1;
	if (script->FindPublic(callback.c_str(), &publicIndex) != AMX_ERR_NONE || publicIndex < 0) return false;

	const std::string format = getAmxStringRaw(amx, formatParam);
	if (params[0] < 0 || params[0] % static_cast<cell>(sizeof(cell)) != 0) return false;
	const size_t supplied = params[0] > 0 ? static_cast<size_t>(params[0]) / sizeof(cell) : 0;
	if (firstParam == 0 || firstParam - 1 > supplied || format.size() != supplied - (firstParam - 1)) return false;

	size_t pendingArray = static_cast<size_t>(-1);
	for (size_t i = 0; i < format.size(); ++i)
	{
		const char kind = format[i];
		const cell parameter = params[firstParam + i];
		if (kind == 'd' || kind == 'i' || kind == 'f' || kind == 'b')
		{
			cell number = 0;
			if (!readVariadicNumber(amx, parameter, number)) return false;
			if (pendingArray != static_cast<size_t>(-1))
			{
				if (number <= 0 || static_cast<size_t>(number) > MAX_CALLBACK_ARRAY_CELLS) return false;
				NativePawnScript* arrayScript = pawnScriptFor(amx);
				if (!arrayScript || !pawnArrayRangeValid(*arrayScript, params[firstParam + i - 1], static_cast<size_t>(number))) return false;
				cell* arrayAddress = nullptr;
				if (pawnGetAddr(amx, params[firstParam + i - 1], &arrayAddress) != AMX_ERR_NONE || !arrayAddress) return false;
				pendingArray = static_cast<size_t>(-1);
			}
			continue;
		}
		if (kind == 's')
		{
			if (pendingArray != static_cast<size_t>(-1)) return false;
			cell* stringAddress = nullptr;
			if (pawnGetAddr(amx, parameter, &stringAddress) != AMX_ERR_NONE || !stringAddress) return false;
			continue;
		}
		if (kind == 'a')
		{
			if (pendingArray != static_cast<size_t>(-1)) return false;
			NativePawnScript* arrayScript = pawnScriptFor(amx);
			if (!arrayScript || !pawnArrayRangeValid(*arrayScript, parameter, 1)) return false;
			cell* arrayAddress = nullptr;
			if (pawnGetAddr(amx, parameter, &arrayAddress) != AMX_ERR_NONE || !arrayAddress) return false;
			pendingArray = i;
			continue;
		}
		if (kind == 'r')
		{
			if (pendingArray != static_cast<size_t>(-1)) return false;
			NativePawnScript* referenceScript = pawnScriptFor(amx);
			// A reference into the active stack belongs to the native caller and
			// may be gone by the time the REST completion runs.  Only retain
			// references in static/data memory; their AMX address is re-resolved
			// when the callback executes.
			if (!referenceScript || parameter < 0 || parameter >= referenceScript->GetHEA()
				|| !pawnArrayRangeValid(*referenceScript, parameter, 1)) return false;
			cell* reference = nullptr;
			if (pawnGetAddr(amx, parameter, &reference) != AMX_ERR_NONE || !reference) return false;
			continue;
		}
		return false;
	}

	return pendingArray == static_cast<size_t>(-1);
}

bool capturePawnCallback(AMX* amx, cell callbackParam, cell formatParam, cell* params, size_t firstParam,
	std::shared_ptr<PreparedPawnCallback>& prepared)
{
	prepared.reset();
	if (!callbackParametersValid(amx, callbackParam, formatParam, params, firstParam)) return false;

	const std::string callback = getAmxStringRaw(amx, callbackParam);
	if (callback.empty()) return true;
	const std::string format = getAmxStringRaw(amx, formatParam);
	if (params[0] < 0 || params[0] % static_cast<cell>(sizeof(cell)) != 0) return false;
	const size_t supplied = params[0] > 0 ? static_cast<size_t>(params[0]) / sizeof(cell) : 0;
	if (firstParam == 0 || firstParam - 1 > supplied || format.size() != supplied - (firstParam - 1)) return false;

	prepared = std::make_shared<PreparedPawnCallback>();
	prepared->dccResult = g_capturingDccResult;
	NativePawnScript* script = pawnScriptFor(amx);
	if (!script) return false;
	prepared->scriptId = script->GetID();
	prepared->name = callback;
	prepared->args.reserve(format.size());

	size_t pendingArray = static_cast<size_t>(-1);
	for (size_t i = 0; i < format.size(); ++i)
	{
		const char kind = format[i];
		const cell parameter = params[firstParam + i];
		if (kind == 'd' || kind == 'i' || kind == 'f' || kind == 'b')
		{
			PawnCallbackArg arg;
			if (!readVariadicNumber(amx, parameter, arg.value)) return false;
			if (pendingArray != static_cast<size_t>(-1))
			{
				if (arg.value <= 0 || static_cast<size_t>(arg.value) > MAX_CALLBACK_ARRAY_CELLS) return false;
				if (!pawnArrayRangeValid(*script, params[firstParam + i - 1], static_cast<size_t>(arg.value))) return false;
				cell* arrayAddress = nullptr;
				if (pawnGetAddr(amx, params[firstParam + i - 1], &arrayAddress) != AMX_ERR_NONE || !arrayAddress) return false;
				prepared->args[pendingArray].array.assign(arrayAddress, arrayAddress + arg.value);
				pendingArray = static_cast<size_t>(-1);
			}
			prepared->args.push_back(std::move(arg));
			continue;
		}
		if (kind == 's')
		{
			if (pendingArray != static_cast<size_t>(-1)) return false;
			PawnCallbackArg arg;
			arg.type = PawnCallbackArg::Type::String;
			arg.text = getAmxStringRaw(amx, parameter);
			prepared->args.push_back(std::move(arg));
			continue;
		}
		if (kind == 'a')
		{
			if (pendingArray != static_cast<size_t>(-1)) return false;
			if (!pawnArrayRangeValid(*script, parameter, 1)) return false;
			cell* arrayAddress = nullptr;
			if (pawnGetAddr(amx, parameter, &arrayAddress) != AMX_ERR_NONE || !arrayAddress) return false;
			PawnCallbackArg arg;
			arg.type = PawnCallbackArg::Type::Array;
			prepared->args.push_back(std::move(arg));
			pendingArray = prepared->args.size() - 1;
			continue;
		}
		if (kind == 'r')
		{
			if (pendingArray != static_cast<size_t>(-1)) return false;
			if (parameter < 0 || parameter >= script->GetHEA() || !pawnArrayRangeValid(*script, parameter, 1)) return false;
			cell* reference = nullptr;
			if (pawnGetAddr(amx, parameter, &reference) != AMX_ERR_NONE || !reference) return false;
			PawnCallbackArg arg;
			arg.type = PawnCallbackArg::Type::Reference;
			arg.referenceAddress = parameter;
			prepared->args.push_back(std::move(arg));
			continue;
		}
		return false;
	}

	return pendingArray == static_cast<size_t>(-1);
}

std::string messagePayload(const std::string& content, const DiscordBuilders::Embed* embed)
{
	DiscordJson body = DiscordJson::object();
	if (!content.empty()) body["content"] = content;
	if (embed) body["embeds"] = DiscordJson::array({ embed->toJson() });
	return body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
}

void completeMessageResponse(bool success, const std::string& responseBody,
	const std::shared_ptr<PreparedPawnCallback>& callback)
{
	if (!callback) return;
	DiscordBridgeComponent* bridge = component();
	if (!bridge) return;

	cell handle = 0;
	std::string createdMessageId;
	if (success)
	{
		if (DiscordMessage* created = bridge->upsertMessageFromJson(responseBody))
		{
			createdMessageId.assign(created->getMessageId().data(), created->getMessageId().length());
			handle = assignMessageHandle(created->getMessageId());
			rememberMessageChannel(handle, created->getChannelId());
		}
	}
	executePawnCallback(*callback, { handle });
	// A REST-created message stays cached only while its callback runs, unless
	// the script marked it with DBR_SetMessagePersistent.  The handle keeps
	// working for message actions because its channel id is remembered.
	if (!createdMessageId.empty())
	{
		if (auto* current = static_cast<DiscordMessage*>(bridge->findMessageById(createdMessageId));
			current && !current->isPersistent())
		{
			bridge->removeMessage(createdMessageId);
		}
	}
}

DiscordChannel* resolveChannelByHandle(cell handle)
{
	auto it = g_channelHandleToId.find(handle);
	if (it == g_channelHandleToId.end())
	{
		return nullptr;
	}

	auto* channel = component()->findChannelById(it->second);
	return static_cast<DiscordChannel*>(channel);
}

std::string channelIdForHandle(cell handle)
{
	const auto it = g_channelHandleToId.find(handle);
	return it == g_channelHandleToId.end() ? std::string() : it->second;
}

std::string userIdForHandle(cell handle)
{
	const auto it = g_userHandleToId.find(handle);
	return it == g_userHandleToId.end() ? std::string() : it->second;
}

std::string guildIdForHandle(cell handle)
{
	const auto it = g_guildHandleToId.find(handle);
	return it == g_guildHandleToId.end() ? std::string() : it->second;
}

std::string roleIdForHandle(cell handle)
{
	const auto it = g_roleHandleToId.find(handle);
	return it == g_roleHandleToId.end() ? std::string() : it->second;
}

void rememberChannelGuild(cell channelHandle, StringView guildId)
{
	if (channelHandle != 0 && !guildId.empty())
	{
		g_channelHandleToGuildId[channelHandle] = std::string(guildId.data(), guildId.length());
	}
}

void rememberMessageChannel(cell handle, StringView channelId)
{
	if (handle != 0 && !channelId.empty())
	{
		g_messageHandleToChannelId[handle] = std::string(channelId.data(), channelId.length());
	}
}

bool messageRefForHandle(cell handle, std::string& channelId, std::string& messageId)
{
	channelId.clear();
	messageId.clear();
	const auto it = g_messageHandleToId.find(handle);
	if (it == g_messageHandleToId.end()) return false;
	messageId = it->second;
	if (DiscordBridgeComponent* bridge = component())
	{
		if (auto* message = static_cast<DiscordMessage*>(bridge->findMessageById(messageId)))
		{
			channelId.assign(message->getChannelId().data(), message->getChannelId().length());
		}
	}
	if (channelId.empty())
	{
		const auto channelIt = g_messageHandleToChannelId.find(handle);
		if (channelIt != g_messageHandleToChannelId.end()) channelId = channelIt->second;
	}
	return !channelId.empty();
}

bool isDiscordSnowflake(const std::string& value)
{
	if (value.size() < 17 || value.size() > 20) return false;
	return std::all_of(value.begin(), value.end(), [](unsigned char character)
	{
		return std::isdigit(character) != 0;
	});
}

DiscordUser* resolveUserByHandle(cell handle)
{
	auto it = g_userHandleToId.find(handle);
	if (it == g_userHandleToId.end())
	{
		return nullptr;
	}

	auto* user = component()->findUserById(it->second);
	return static_cast<DiscordUser*>(user);
}

DiscordGuild* resolveGuildByHandle(cell handle)
{
	auto it = g_guildHandleToId.find(handle);
	if (it == g_guildHandleToId.end())
	{
		return nullptr;
	}

	auto* guild = component()->findGuildById(it->second);
	return static_cast<DiscordGuild*>(guild);
}

DiscordMessage* resolveMessageByHandle(cell handle)
{
	auto it = g_messageHandleToId.find(handle);
	if (it == g_messageHandleToId.end())
	{
		return nullptr;
	}

	auto* message = component()->findMessageById(it->second);
	return static_cast<DiscordMessage*>(message);
}

DiscordRole* resolveRoleByHandle(cell handle)
{
	auto it = g_roleHandleToId.find(handle);
	if (it == g_roleHandleToId.end())
	{
		return nullptr;
	}

	return component()->findRoleByIdInternal(it->second);
}

std::string resolveEmojiToken(cell handle)
{
	const auto it = g_emojiHandleToToken.find(handle);
	return it == g_emojiHandleToToken.end() ? std::string() : it->second;
}

cell AMX_NATIVE_CALL Native_ConnectDiscordBot(AMX* amx, cell* params)
{
	if (params[0] < static_cast<cell>(2 * sizeof(cell)))
	{
		return 0;
	}

	const std::string token = getAmxString(amx, params[1]);
	// DBR_ConnectBot(token[], DiscordIntent:intents) has two parameters.
	const int intents = nativeParamCount(params) >= 2 ? static_cast<int>(params[2]) : DISCORD_DEFAULT_INTENTS;
	DiscordBridgeComponent* bridge = component();
	if (!bridge)
	{
		return 0;
	}

	// A token outside the script (environment variable or server
	// configuration) always wins, whichever of the two loads first.
	bridge->loadConfiguration();
	// DBR_DisconnectBot followed by DBR_ConnectBot in the same tick: reconnect
	// once the old bot has been torn down.
	if (bridge->isDisconnectPending())
	{
		if (!bridge->hasConfiguredToken() && token.empty()) return 0;
		bridge->queueReconnect(token, intents);
		return 1;
	}
	if (bridge->hasConfiguredToken())
	{
		if (!g_connectIgnoredWarned)
		{
			g_connectIgnoredWarned = true;
			logWarning("[DiscordBridge] DBR_ConnectBot was ignored: the token set in DISCORD_BOT_TOKEN or the server "
				"configuration (discord_bot_token) takes priority. Choose the intents there with discord_bot_intents.");
		}
		return bridge->connectConfiguredBot() ? 1 : 0;
	}
	if (token.empty())
	{
		return 0;
	}

	return bridge->connectBot(token, intents) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_DisconnectBot(AMX*, cell*)
{
	DiscordBridgeComponent* bridge = component();
	return bridge && bridge->requestDisconnect() ? 1 : 0;
}


cell AMX_NATIVE_CALL Native_SetTextEncoding(AMX*, cell* params)
{
	if (nativeParamCount(params) < 1 || (params[1] != TEXT_ENCODING_ANSI && params[1] != TEXT_ENCODING_UTF8)) return 0;
	g_textEncoding = params[1];
	return 1;
}

cell AMX_NATIVE_CALL Native_GetTextEncoding(AMX*, cell*)
{
	return g_textEncoding;
}

cell AMX_NATIVE_CALL Native_SetDebugMode(AMX*, cell* params)
{
	g_debugMode = nativeParamCount(params) >= 1 && params[1] != 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_IsDebugMode(AMX*, cell*)
{
	return g_debugMode ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_IsDiscordConnected(AMX*, cell*)
{
	auto* bot = component()->getBot();
	return (bot && bot->isConnected()) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_FindChannelById(AMX* amx, cell* params)
{
	const std::string channelId = getAmxString(amx, params[1]);
	// Handles are backed by the snowflake, so scripts can resolve IDs during
	// OnGameModeInit.  The channel object may arrive later through GUILD_CREATE,
	// but the handle is already safe to queue outbound work against.
	return isDiscordSnowflake(channelId) ? assignChannelHandle(channelId) : 0;
}

cell AMX_NATIVE_CALL Native_FindChannelByName(AMX* amx, cell* params)
{
	const std::string name = getAmxString(amx, params[1]);
	auto* channel = static_cast<DiscordChannel*>(component()->findChannelByName(name));
	if (!channel)
	{
		return 0;
	}

	return assignChannelHandle(channel->getChannelId());
}

cell AMX_NATIVE_CALL Native_FindDiscordConfiguredChannel(AMX*, cell*)
{
	if (component())
	{
		const StringView configuredId = component()->configuredChannelId();
		if (!configuredId.empty())
		{
			const std::string id(configuredId.data(), configuredId.length());
			return isDiscordSnowflake(id) ? assignChannelHandle(id) : 0;
		}
	}

	auto* channel = static_cast<DiscordChannel*>(component()->findConfiguredChannel());
	if (!channel)
	{
		return 0;
	}

	return assignChannelHandle(channel->getChannelId());
}

cell AMX_NATIVE_CALL Native_GetChannelId(AMX* amx, cell* params)
{
	if (params[0] < static_cast<cell>(3 * sizeof(cell)))
	{
		return 0;
	}

	const std::string channelId = channelIdForHandle(params[1]);
	if (channelId.empty())
	{
		return 0;
	}

	if (params[3] < 0) return 0;
	size_t maxSize = static_cast<size_t>(params[3]);
	if (maxSize == 0)
	{
		maxSize = 21;
	}

	return setAmxString(amx, params[2], channelId, maxSize) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetChannelName(AMX* amx, cell* params)
{
	if (params[0] < static_cast<cell>(3 * sizeof(cell)))
	{
		return 0;
	}

	DiscordChannel* channel = resolveChannelByHandle(params[1]);
	if (!channel)
	{
		return 0;
	}

	if (params[3] < 0) return 0;
	size_t maxSize = static_cast<size_t>(params[3]);
	const std::string name(channel->getChannelName().data(), channel->getChannelName().length());
	return setAmxString(amx, params[2], name, maxSize) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetChannelTopic(AMX* amx, cell* params)
{
	if (params[0] < static_cast<cell>(3 * sizeof(cell)))
	{
		return 0;
	}

	DiscordChannel* channel = resolveChannelByHandle(params[1]);
	if (!channel)
	{
		return 0;
	}

	if (params[3] < 0) return 0;
	size_t maxSize = static_cast<size_t>(params[3]);
	const std::string topic(channel->getTopic().data(), channel->getTopic().length());
	return setAmxString(amx, params[2], topic, maxSize) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetChannelType(AMX* amx, cell* params)
{
	DiscordChannel* channel = resolveChannelByHandle(params[1]);
	if (!channel)
	{
		return 0;
	}

	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}

	*out = static_cast<cell>(channel->getChannelType());
	return 1;
}

cell AMX_NATIVE_CALL Native_SendChannelMessage(AMX* amx, cell* params)
{
	const std::string channelId = channelIdForHandle(params[1]);
	const std::string message = getAmxString(amx, params[2]);
	if (channelId.empty() || message.empty() || message.size() > 2000) return 0;
	std::shared_ptr<PreparedPawnCallback> callback;
	if (nativeParamCount(params) >= 4 && !capturePawnCallback(amx, params[3], params[4], params, 5, callback)) return 0;
	DiscordBot* bot = nativeBot();
	if (!bot) return 0;
	std::function<void(const DiscordHTTP::Response&)> completion;
	if (callback) completion = [bot, callback](const DiscordHTTP::Response& response)
	{
		bot->enqueueCompletion([response, callback]()
		{
			completeMessageResponse(response.success, response.body, callback);
		});
	};
	return bot->sendChannelMessage(channelId, message, std::move(completion)) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetChannelName(AMX* amx, cell* params)
{
	const std::string channelId = channelIdForHandle(params[1]);
	const std::string name = getAmxString(amx, params[2]);
	if (name.size() < 2 || name.size() > 100)
	{
		return 0;
	}

	DiscordBot* bot = nativeBot();
	if (channelId.empty() || !bot) return 0;
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("name", name) + "}";
	return bot->submitRestTask([channelId, body](DiscordHTTP& http)
	{
		http.modifyChannel(channelId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetChannelTopic(AMX* amx, cell* params)
{
	const std::string channelId = channelIdForHandle(params[1]);
	const std::string topic = getAmxString(amx, params[2]);
	if (topic.size() > 1024)
	{
		return 0;
	}

	DiscordBot* bot = nativeBot();
	if (channelId.empty() || !bot) return 0;
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("topic", topic) + "}";
	return bot->submitRestTask([channelId, body](DiscordHTTP& http)
	{
		http.modifyChannel(channelId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_DeleteChannel(AMX* amx, cell* params)
{
	(void)amx;
	const std::string channelId = channelIdForHandle(params[1]);
	DiscordBot* bot = nativeBot();
	if (channelId.empty() || !bot) return 0;
	return bot->submitRestTask([channelId](DiscordHTTP& http)
	{
		http.deleteChannel(channelId);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_FindUserById(AMX* amx, cell* params)
{
	const std::string userId = getAmxString(amx, params[1]);
	return isDiscordSnowflake(userId) ? assignUserHandle(userId) : 0;
}

cell AMX_NATIVE_CALL Native_FindUserByName(AMX* amx, cell* params)
{
	const std::string name = getAmxString(amx, params[1]);
	const std::string disc = nativeParamCount(params) >= 2 ? getAmxString(amx, params[2]) : std::string();

	auto* user = component()->findUserByNameAndDiscriminator(name, disc);
	if (!user)
	{
		return 0;
	}

	return assignUserHandle(user->getUserId());
}

cell AMX_NATIVE_CALL Native_GetUserId(AMX* amx, cell* params)
{
	const auto it = g_userHandleToId.find(params[1]);
	if (it == g_userHandleToId.end())
	{
		return 0;
	}
	return setAmxString(amx, params[2], it->second, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetUserName(AMX* amx, cell* params)
{
	DiscordUser* user = resolveUserByHandle(params[1]);
	if (!user)
	{
		return 0;
	}
	const std::string name(user->getUsername().data(), user->getUsername().length());
	return setAmxString(amx, params[2], name, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetUserDiscriminator(AMX* amx, cell* params)
{
	if (nativeParamCount(params) < 3) return 0;
	DiscordUser* user = resolveUserByHandle(params[1]);
	if (!user) return 0;
	const auto discriminator = user->getDiscriminator();
	return setAmxString(amx, params[2], std::string(discriminator.data(), discriminator.length()), params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_IsUserBot(AMX* amx, cell* params)
{
	DiscordUser* user = resolveUserByHandle(params[1]);
	if (!user)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}
	*out = user->isBot() ? 1 : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_FindGuildById(AMX* amx, cell* params)
{
	const std::string guildId = getAmxString(amx, params[1]);
	return isDiscordSnowflake(guildId) ? assignGuildHandle(guildId) : 0;
}

cell AMX_NATIVE_CALL Native_FindGuildByName(AMX* amx, cell* params)
{
	const std::string name = getAmxString(amx, params[1]);
	auto* guild = static_cast<DiscordGuild*>(component()->findGuildByName(name));
	if (!guild)
	{
		return 0;
	}
	return assignGuildHandle(guild->getGuildId());
}

cell AMX_NATIVE_CALL Native_GetGuildId(AMX* amx, cell* params)
{
	const auto it = g_guildHandleToId.find(params[1]);
	if (it == g_guildHandleToId.end())
	{
		return 0;
	}
	return setAmxString(amx, params[2], it->second, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetGuildName(AMX* amx, cell* params)
{
	DiscordGuild* guild = resolveGuildByHandle(params[1]);
	if (!guild)
	{
		return 0;
	}
	const std::string name(guild->getGuildName().data(), guild->getGuildName().length());
	return setAmxString(amx, params[2], name, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetGuildOwnerId(AMX* amx, cell* params)
{
	DiscordGuild* guild = resolveGuildByHandle(params[1]);
	if (!guild)
	{
		return 0;
	}
	const std::string owner(guild->getOwnerId().data(), guild->getOwnerId().length());
	return setAmxString(amx, params[2], owner, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetMessageId(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	if (!message)
	{
		return 0;
	}
	const std::string id(message->getMessageId().data(), message->getMessageId().length());
	return setAmxString(amx, params[2], id, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetMessageChannel(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	if (!message)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}

	const std::string channelId(message->getChannelId().data(), message->getChannelId().length());
	*out = isDiscordSnowflake(channelId) ? assignChannelHandle(channelId) : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_GetMessageAuthor(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	if (!message)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}

	auto* user = static_cast<DiscordUser*>(component()->findUserById(message->getAuthorId()));
	if (!user)
	{
		*out = 0;
		return 1;
	}

	*out = assignUserHandle(user->getUserId());
	return 1;
}

cell AMX_NATIVE_CALL Native_GetMessageContent(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	if (!message)
	{
		return 0;
	}
	const std::string content(message->getContent().data(), message->getContent().length());
	return setAmxString(amx, params[2], content, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_IsMessageTts(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	if (!message)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}
	*out = message->isTTS() ? 1 : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_IsMessageMentioningEveryone(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	if (!message)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}
	*out = message->mentionsEveryone() ? 1 : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_DeleteMessage(AMX*, cell* params)
{
	std::string channelId;
	std::string messageId;
	DiscordBot* bot = nativeBot();
	if (!bot || !messageRefForHandle(params[1], channelId, messageId)) return 0;
	return bot->submitRestTask([channelId, messageId](DiscordHTTP& http)
	{
		http.deleteMessage(channelId, messageId);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_EditMessage(AMX* amx, cell* params)
{
	std::string channelId;
	std::string messageId;
	DiscordBot* bot = nativeBot();
	if (!bot || !messageRefForHandle(params[1], channelId, messageId)) return 0;
	const std::string content = getAmxString(amx, params[2]);
	if (content.size() > 2000) return 0;
	auto embedIt = g_embeds.end();
	if (params[3] != 0)
	{
		embedIt = g_embeds.find(params[3]);
		if (embedIt == g_embeds.end()) return 0;
	}
	const std::string body = messagePayload(content, embedIt == g_embeds.end() ? nullptr : &embedIt->second);
	const bool queued = bot->submitRestTask([channelId, messageId, body](DiscordHTTP& http)
	{
		http.editMessagePayload(channelId, messageId, body);
	});
	if (queued && embedIt != g_embeds.end()) g_embeds.erase(embedIt);
	return queued ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_FindRoleById(AMX* amx, cell* params)
{
	const std::string roleId = getAmxString(amx, params[1]);
	return isDiscordSnowflake(roleId) ? assignRoleHandle(roleId) : 0;
}

cell AMX_NATIVE_CALL Native_FindRoleByName(AMX* amx, cell* params)
{
	const cell guildHandle = params[1];
	const std::string roleName = getAmxString(amx, params[2]);
	if (roleName.empty())
	{
		return 0;
	}

	std::string guildId;
	auto guildIt = g_guildHandleToId.find(guildHandle);
	if (guildIt != g_guildHandleToId.end())
	{
		guildId = guildIt->second;
	}

	DiscordRole* role = component()->findRoleByNameInternal(roleName, guildId);
	if (!role)
	{
		return 0;
	}
	return assignRoleHandle(role->getRoleId());
}

cell AMX_NATIVE_CALL Native_GetRoleId(AMX* amx, cell* params)
{
	const auto it = g_roleHandleToId.find(params[1]);
	if (it == g_roleHandleToId.end())
	{
		return 0;
	}
	return setAmxString(amx, params[2], it->second, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetRoleName(AMX* amx, cell* params)
{
	DiscordRole* role = resolveRoleByHandle(params[1]);
	if (!role)
	{
		return 0;
	}
	const std::string name(role->getRoleName().data(), role->getRoleName().length());
	return setAmxString(amx, params[2], name, params[3]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetRoleColor(AMX* amx, cell* params)
{
	DiscordRole* role = resolveRoleByHandle(params[1]);
	if (!role)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}
	*out = static_cast<cell>(role->getColor());
	return 1;
}

cell AMX_NATIVE_CALL Native_GetRolePermissions(AMX* amx, cell* params)
{
	DiscordRole* role = resolveRoleByHandle(params[1]);
	cell* high = nullptr;
	cell* low = nullptr;
	if (pawnGetAddr(amx, params[2], &high) != AMX_ERR_NONE || !high)
	{
		return 0;
	}
	if (pawnGetAddr(amx, params[3], &low) != AMX_ERR_NONE || !low)
	{
		return 0;
	}
	if (!role) return 0;
	const uint64_t permissions = role->getPermissions();
	*high = static_cast<cell>((permissions >> 32U) & 0xFFFFFFFFULL);
	*low = static_cast<cell>(permissions & 0xFFFFFFFFULL);
	return 1;
}

cell AMX_NATIVE_CALL Native_IsRoleHoist(AMX* amx, cell* params)
{
	DiscordRole* role = resolveRoleByHandle(params[1]);
	if (!role)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}
	*out = role->isHoisted() ? 1 : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_GetRolePosition(AMX* amx, cell* params)
{
	DiscordRole* role = resolveRoleByHandle(params[1]);
	if (!role)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}
	*out = static_cast<cell>(role->getPosition());
	return 1;
}

cell AMX_NATIVE_CALL Native_IsRoleMentionable(AMX* amx, cell* params)
{
	DiscordRole* role = resolveRoleByHandle(params[1]);
	if (!role)
	{
		return 0;
	}
	cell* out = nullptr;
	if (pawnGetAddr(amx, params[2], &out) != AMX_ERR_NONE || !out)
	{
		return 0;
	}
	*out = role->isMentionable() ? 1 : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_CreateEmoji(AMX* amx, cell* params)
{
	const std::string name = getAmxString(amx, params[1]);
	const std::string snowflake = getAmxString(amx, params[2]);
	if (name.empty())
	{
		return 0;
	}
	return CreateDiscordEmojiHandle(name, snowflake);
}

cell AMX_NATIVE_CALL Native_DeleteEmoji(AMX* amx, cell* params)
{
	const cell handle = params[1];
	return g_emojiHandleToToken.erase(handle) > 0 ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetEmojiName(AMX* amx, cell* params)
{
	const std::string token = resolveEmojiToken(params[1]);
	if (token.empty())
	{
		return 0;
	}
	const size_t pos = token.find(':');
	const std::string name = pos == std::string::npos ? token : token.substr(0, pos);
	if (!setAmxString(amx, params[2], name, params[3])) return -1;
	return static_cast<cell>(name.length());
}

cell AMX_NATIVE_CALL Native_CreateReaction(AMX*, cell* params)
{
	std::string channelId;
	std::string messageId;
	DiscordBot* bot = nativeBot();
	const std::string token = resolveEmojiToken(params[2]);
	if (!bot || token.empty() || !messageRefForHandle(params[1], channelId, messageId)) return 0;
	const bool queued = bot->submitRestTask([channelId, messageId, token](DiscordHTTP& http)
	{
		http.addReaction(channelId, messageId, token);
	});
	// Creating a reaction consumes the temporary emoji handle.
	DeleteDiscordEmojiHandle(params[2]);
	return queued ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_DeleteMessageReaction(AMX*, cell* params)
{
	std::string channelId;
	std::string messageId;
	DiscordBot* bot = nativeBot();
	if (!bot || !messageRefForHandle(params[1], channelId, messageId)) return 0;
	const cell emojiHandle = params[2];
	if (emojiHandle == 0)
	{
		return bot->submitRestTask([channelId, messageId](DiscordHTTP& http)
		{
			http.deleteAllReactions(channelId, messageId);
		}) ? 1 : 0;
	}
	const std::string token = resolveEmojiToken(emojiHandle);
	if (token.empty()) return 0;
	return bot->submitRestTask([channelId, messageId, token](DiscordHTTP& http)
	{
		http.deleteEmojiReactions(channelId, messageId, token);
	}) ? 1 : 0;
}

cell* nativeRef(AMX* amx, cell address)
{
	cell* out = nullptr;
	return pawnGetAddr(amx, address, &out) == AMX_ERR_NONE ? out : nullptr;
}

DiscordBot* nativeBot()
{
	if (!component() || !component()->getBot()) return nullptr;
	return static_cast<DiscordBot*>(component()->getBot());
}

DiscordGuild* guildForHandle(cell handle)
{
	return resolveGuildByHandle(handle);
}

DiscordRole* roleForHandle(cell handle)
{
	return resolveRoleByHandle(handle);
}

cell AMX_NATIVE_CALL Native_GetChannelGuild(AMX* amx, cell* params)
{
	DiscordChannel* channel = resolveChannelByHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!out) return 0;

	std::string guildId;
	if (channel)
	{
		guildId.assign(channel->getGuildId().data(), channel->getGuildId().length());
	}
	if (guildId.empty())
	{
		const auto it = g_channelHandleToGuildId.find(params[1]);
		if (it != g_channelHandleToGuildId.end()) guildId = it->second;
		else if (!channel) return 0;
	}

	*out = guildId.empty() ? 0 : assignGuildHandle(guildId);
	return 1;
}

cell AMX_NATIVE_CALL Native_GetChannelPosition(AMX* amx, cell* params)
{
	DiscordChannel* channel = resolveChannelByHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!channel || !out) return 0;
	*out = static_cast<cell>(channel->getPosition());
	return 1;
}

cell AMX_NATIVE_CALL Native_IsChannelNsfw(AMX* amx, cell* params)
{
	DiscordChannel* channel = resolveChannelByHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!channel || !out) return 0;
	*out = channel->isNSFW() ? 1 : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_GetChannelParentCategory(AMX* amx, cell* params)
{
	DiscordChannel* channel = resolveChannelByHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!channel || !out) return 0;
	const std::string parent(channel->getParentId().data(), channel->getParentId().length());
	*out = parent.empty() ? 0 : assignChannelHandle(parent);
	return 1;
}

cell AMX_NATIVE_CALL Native_SetChannelPosition(AMX*, cell* params)
{
	const std::string channelId = channelIdForHandle(params[1]);
	DiscordBot* bot = nativeBot();
	if (channelId.empty() || !bot) return 0;
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("position", static_cast<int64_t>(params[2])) + "}";
	return bot->submitRestTask([channelId, body](DiscordHTTP& http)
	{
		http.modifyChannel(channelId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetChannelNsfw(AMX*, cell* params)
{
	const std::string channelId = channelIdForHandle(params[1]);
	DiscordBot* bot = nativeBot();
	if (channelId.empty() || !bot) return 0;
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("nsfw", params[2] != 0) + "}";
	return bot->submitRestTask([channelId, body](DiscordHTTP& http)
	{
		http.modifyChannel(channelId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetChannelParentCategory(AMX*, cell* params)
{
	const std::string channelId = channelIdForHandle(params[1]);
	const std::string parentId = channelIdForHandle(params[2]);
	DiscordBot* bot = nativeBot();
	if (channelId.empty() || parentId.empty() || !bot) return 0;
	if (auto* parent = resolveChannelByHandle(params[2]); parent && parent->getChannelType() != EDiscordChannelType::GuildCategory) return 0;
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("parent_id", parentId) + "}";
	return bot->submitRestTask([channelId, body](DiscordHTTP& http)
	{
		http.modifyChannel(channelId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetMessageUserMentionCount(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!message || !out) return 0;
	*out = static_cast<cell>(message->getUserMentionIds().size());
	return 1;
}

cell AMX_NATIVE_CALL Native_GetMessageUserMention(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	cell* out = nativeRef(amx, params[3]);
	if (!message || !out || params[2] < 0 || static_cast<size_t>(params[2]) >= message->getUserMentionIds().size()) return 0;
	*out = assignUserHandle(message->getUserMentionIds()[static_cast<size_t>(params[2])]);
	return 1;
}

cell AMX_NATIVE_CALL Native_GetMessageRoleMentionCount(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!message || !out) return 0;
	*out = static_cast<cell>(message->getRoleMentionIds().size());
	return 1;
}

cell AMX_NATIVE_CALL Native_GetMessageRoleMention(AMX* amx, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	cell* out = nativeRef(amx, params[3]);
	if (!message || !out || params[2] < 0 || static_cast<size_t>(params[2]) >= message->getRoleMentionIds().size()) return 0;
	*out = assignRoleHandle(message->getRoleMentionIds()[static_cast<size_t>(params[2])]);
	return 1;
}

cell AMX_NATIVE_CALL Native_IsUserVerified(AMX* amx, cell* params)
{
	DiscordUser* user = resolveUserByHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!user || !out) return 0;
	*out = user->isVerified() ? 1 : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildRole(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	cell* out = nativeRef(amx, params[3]);
	if (!guild || !out || params[2] < 0) return 0;
	const std::string* id = guild->getRoleIdAt(static_cast<size_t>(params[2]));
	if (!id) return 0;
	*out = assignRoleHandle(*id);
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildRoleCount(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!guild || !out) return 0;
	*out = static_cast<cell>(guild->getRoleIds().size());
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildMember(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	cell* out = nativeRef(amx, params[3]);
	if (!guild || !out || params[2] < 0) return 0;
	const std::string* id = guild->getMemberIdAt(static_cast<size_t>(params[2]));
	if (!id) return 0;
	if (!component()->findUserById(*id)) return 0;
	*out = assignUserHandle(*id);
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberCount(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!guild || !out) return 0;
	*out = static_cast<cell>(guild->getMemberIds().size());
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberVoiceChannel(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordUser* user = resolveUserByHandle(params[2]);
	cell* out = nativeRef(amx, params[3]);
	if (!guild || !user || !out) return 0;
	const std::string userId(user->getUserId().data(), user->getUserId().length());
	DiscordGuild::Member* member = guild->findMember(userId);
	if (!member) return 0;
	*out = member->voiceChannelId.empty() ? 0 : assignChannelHandle(member->voiceChannelId);
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberNickname(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordUser* user = resolveUserByHandle(params[2]);
	if (!guild || !user) return 0;
	DiscordGuild::Member* member = guild->findMember(user->getUserId());
	if (!member) return 0;
	return setAmxString(amx, params[3], member->nickname, params[4]) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberRole(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordUser* user = resolveUserByHandle(params[2]);
	cell* out = nativeRef(amx, params[4]);
	if (!guild || !user || !out || params[3] < 0) return 0;
	DiscordGuild::Member* member = guild->findMember(user->getUserId());
	if (!member || params[3] < 0 || static_cast<size_t>(params[3]) >= member->roleIds.size()) return 0;
	*out = assignRoleHandle(member->roleIds[static_cast<size_t>(params[3])]);
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberRoleCount(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordUser* user = resolveUserByHandle(params[2]);
	cell* out = nativeRef(amx, params[3]);
	if (!guild || !user || !out) return 0;
	DiscordGuild::Member* member = guild->findMember(user->getUserId());
	if (!member) return 0;
	*out = static_cast<cell>(member->roleIds.size());
	return 1;
}

cell AMX_NATIVE_CALL Native_HasGuildMemberRole(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordUser* user = resolveUserByHandle(params[2]);
	DiscordRole* role = roleForHandle(params[3]);
	cell* out = nativeRef(amx, params[4]);
	if (!guild || !user || !role || !out) return 0;
	DiscordGuild::Member* member = guild->findMember(user->getUserId());
	if (!member) return 0;
	const std::string roleId(role->getRoleId().data(), role->getRoleId().length());
	*out = std::find(member->roleIds.begin(), member->roleIds.end(), roleId) != member->roleIds.end() ? 1 : 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberStatus(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordUser* user = resolveUserByHandle(params[2]);
	cell* out = nativeRef(amx, params[3]);
	if (!guild || !user || !out) return 0;
	DiscordGuild::Member* member = guild->findMember(user->getUserId());
	if (!member) return 0;
	*out = static_cast<cell>(member->presenceStatus);
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildChannel(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	cell* out = nativeRef(amx, params[3]);
	if (!guild || !out || params[2] < 0) return 0;
	const std::string* id = guild->getChannelIdAt(static_cast<size_t>(params[2]));
	if (!id) return 0;
	*out = assignChannelHandle(*id);
	return 1;
}

cell AMX_NATIVE_CALL Native_GetGuildChannelCount(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	cell* out = nativeRef(amx, params[2]);
	if (!guild || !out) return 0;
	*out = static_cast<cell>(guild->getChannelIds().size());
	return 1;
}

cell AMX_NATIVE_CALL Native_GetAllGuilds(AMX* amx, cell* params)
{
	cell* out = nativeRef(amx, params[1]);
	if (!out || params[2] < 0) return 0;
	const size_t maxSize = static_cast<size_t>(params[2]);
	const std::vector<std::string> guildIds = component()->getKnownGuildIds();
	const size_t count = std::min(maxSize, guildIds.size());
	for (size_t i = 0; i < count; ++i) out[i] = assignGuildHandle(guildIds[i]);
	return static_cast<cell>(count);
}

cell AMX_NATIVE_CALL Native_SetGuildName(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	if (!guild) return 0;
	const std::string name = getAmxString(amx, params[2]);
	if (name.size() < 2 || name.size() > 100) return 0;
	return guild->setName(name) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_CreateGuildChannel(AMX* amx, cell* params)
{
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string name = getAmxString(amx, params[2]);
	const auto type = static_cast<EDiscordChannelType>(params[3]);
	if (guildId.empty() || name.empty() || name.size() > 100) return 0;
	if (type != EDiscordChannelType::GuildCategory && type != EDiscordChannelType::GuildText &&
		type != EDiscordChannelType::GuildVoice && type != EDiscordChannelType::GuildNews &&
		type != EDiscordChannelType::GuildStageVoice && type != EDiscordChannelType::GuildForum) return 0;
	std::shared_ptr<PreparedPawnCallback> callback;
	if (!capturePawnCallback(amx, params[4], params[5], params, 6, callback)) return 0;
	DiscordBot* bot = nativeBot();
	if (!bot) return 0;
	const std::string body = DiscordJson { { "name", name }, { "type", static_cast<int>(type) } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
	return bot->submitRestTask([bot, guildId, body, callback](DiscordHTTP& rest)
	{
		const auto response = rest.createGuildChannel(guildId, body);
		bot->enqueueCompletion([response, callback]()
		{
			DiscordBridgeComponent* bridge = component();
			DiscordChannel* channel = response.success && bridge ? bridge->upsertChannelFromJson(response.body) : nullptr;
			if (callback) executePawnCallback(*callback, { channel ? assignChannelHandle(channel->getChannelId()) : 0 });
		});
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetGuildRolePosition(AMX*, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordRole* role = roleForHandle(params[2]);
	DiscordBot* bot = nativeBot();
	if (!guild || !role || !bot) return 0;
	const std::string guildId(guild->getGuildId().data(), guild->getGuildId().length());
	const std::string roleId(role->getRoleId().data(), role->getRoleId().length());
	const int position = static_cast<int>(params[3]);
	return bot->submitRestTask([guildId, roleId, position](DiscordHTTP& http)
	{
		http.modifyGuildRolePosition(guildId, roleId, position);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetGuildRoleName(AMX* amx, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordRole* role = roleForHandle(params[2]);
	DiscordBot* bot = nativeBot();
	if (!guild || !role || !bot) return 0;
	const std::string name = getAmxString(amx, params[3]);
	const std::string guildId(guild->getGuildId().data(), guild->getGuildId().length());
	const std::string roleId(role->getRoleId().data(), role->getRoleId().length());
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("name", name) + "}";
	return bot->submitRestTask([guildId, roleId, body](DiscordHTTP& http)
	{
		http.modifyGuildRole(guildId, roleId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetGuildRolePermissions(AMX*, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordRole* role = roleForHandle(params[2]);
	DiscordBot* bot = nativeBot();
	if (!guild || !role || !bot) return 0;
	const uint64_t high = static_cast<uint32_t>(params[3]);
	const uint64_t low = static_cast<uint32_t>(params[4]);
	const uint64_t permissions = (high << 32U) | low;
	const std::string guildId(guild->getGuildId().data(), guild->getGuildId().length());
	const std::string roleId(role->getRoleId().data(), role->getRoleId().length());
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("permissions", std::to_string(permissions)) + "}";
	return bot->submitRestTask([guildId, roleId, body](DiscordHTTP& http)
	{
		http.modifyGuildRole(guildId, roleId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetGuildRoleColor(AMX*, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordRole* role = roleForHandle(params[2]);
	DiscordBot* bot = nativeBot();
	if (!guild || !role || !bot) return 0;
	const int color = static_cast<int>(params[3]);
	const std::string guildId(guild->getGuildId().data(), guild->getGuildId().length());
	const std::string roleId(role->getRoleId().data(), role->getRoleId().length());
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("color", static_cast<int64_t>(color)) + "}";
	return bot->submitRestTask([guildId, roleId, body](DiscordHTTP& http)
	{
		http.modifyGuildRole(guildId, roleId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetGuildRoleHoist(AMX*, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordRole* role = roleForHandle(params[2]);
	DiscordBot* bot = nativeBot();
	if (!guild || !role || !bot) return 0;
	const bool value = params[3] != 0;
	const std::string guildId(guild->getGuildId().data(), guild->getGuildId().length());
	const std::string roleId(role->getRoleId().data(), role->getRoleId().length());
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("hoist", value) + "}";
	return bot->submitRestTask([guildId, roleId, body](DiscordHTTP& http)
	{
		http.modifyGuildRole(guildId, roleId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetGuildRoleMentionable(AMX*, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordRole* role = roleForHandle(params[2]);
	DiscordBot* bot = nativeBot();
	if (!guild || !role || !bot) return 0;
	const bool value = params[3] != 0;
	const std::string guildId(guild->getGuildId().data(), guild->getGuildId().length());
	const std::string roleId(role->getRoleId().data(), role->getRoleId().length());
	const std::string body = std::string("{") + DiscordUtils::buildJsonPair("mentionable", value) + "}";
	return bot->submitRestTask([guildId, roleId, body](DiscordHTTP& http)
	{
		http.modifyGuildRole(guildId, roleId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_CreateGuildRole(AMX* amx, cell* params)
{
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string name = getAmxString(amx, params[2]);
	if (guildId.empty() || name.empty() || name.size() > 100) return 0;
	std::shared_ptr<PreparedPawnCallback> callback;
	if (!capturePawnCallback(amx, params[3], params[4], params, 5, callback)) return 0;
	DiscordBot* bot = nativeBot();
	if (!bot) return 0;
	const std::string body = DiscordJson { { "name", name } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
	return bot->submitRestTask([bot, guildId, body, callback](DiscordHTTP& rest)
	{
		const auto response = rest.createGuildRole(guildId, body);
		bot->enqueueCompletion([response, guildId, callback]()
		{
			DiscordBridgeComponent* bridge = component();
			DiscordRole* role = response.success && bridge ? bridge->upsertRoleFromJson(response.body, guildId) : nullptr;
			if (callback) executePawnCallback(*callback, { role ? assignRoleHandle(role->getRoleId()) : 0 });
		});
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_DeleteGuildRole(AMX*, cell* params)
{
	DiscordGuild* guild = guildForHandle(params[1]);
	DiscordRole* role = roleForHandle(params[2]);
	DiscordBot* bot = nativeBot();
	if (!guild || !role || !bot) return 0;
	const std::string guildId(guild->getGuildId().data(), guild->getGuildId().length());
	const std::string roleId(role->getRoleId().data(), role->getRoleId().length());
	return bot->submitRestTask([guildId, roleId](DiscordHTTP& http)
	{
		http.deleteGuildRole(guildId, roleId);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_DeleteInternalMessage(AMX*, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	if (!message) return 0;
	component()->removeMessage(message->getMessageId());
	return 1;
}

cell AMX_NATIVE_CALL Native_SetMessagePersistent(AMX*, cell* params)
{
	DiscordMessage* message = resolveMessageByHandle(params[1]);
	if (!message) return 0;
	message->setPersistent(params[2] != 0);
	return 1;
}

cell AMX_NATIVE_CALL Native_TriggerBotTypingIndicator(AMX*, cell* params)
{
	const std::string id = channelIdForHandle(params[1]);
	DiscordBot* bot = nativeBot();
	if (id.empty() || !bot) return 0;
	return bot->submitRestTask([id](DiscordHTTP& http)
	{
		http.triggerTyping(id);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_CreatePrivateChannel(AMX* amx, cell* params)
{
	const std::string userId = userIdForHandle(params[1]);
	if (userId.empty()) return 0;
	std::shared_ptr<PreparedPawnCallback> callback;
	if (!capturePawnCallback(amx, params[2], params[3], params, 4, callback)) return 0;
	DiscordBot* bot = nativeBot();
	if (!bot) return 0;
	return bot->submitRestTask([bot, userId, callback](DiscordHTTP& rest)
	{
		const auto response = rest.createDM(userId);
		bot->enqueueCompletion([response, callback]()
		{
			DiscordBridgeComponent* bridge = component();
			DiscordChannel* channel = response.success && bridge ? bridge->upsertChannelFromJson(response.body) : nullptr;
			if (callback) executePawnCallback(*callback, { channel ? assignChannelHandle(channel->getChannelId()) : 0 });
		});
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_EscapeMarkdown(AMX* amx, cell* params)
{
	const std::string source = getAmxString(amx, params[1]);
	std::string escaped;
	escaped.reserve(source.size() * 2);
	bool escapedByUser = false;
	for (char ch : source)
	{
		if (ch == '\\')
		{
			escapedByUser = true;
			escaped.push_back(ch);
			continue;
		}
		if (std::string("_*~`|").find(ch) != std::string::npos && !escapedByUser) escaped.push_back('\\');
		escaped.push_back(ch);
		escapedByUser = false;
	}
	if (!setAmxString(amx, params[2], escaped, params[3])) return 0;
	return static_cast<cell>(escaped.size());
}

cell AMX_NATIVE_CALL Native_CreateEmbed(AMX* amx, cell* params)
{
	DiscordBuilders::Embed embed;
	embed.title = getAmxString(amx, params[1]);
	embed.description = getAmxString(amx, params[2]);
	embed.url = getAmxString(amx, params[3]);
	embed.timestamp = getAmxString(amx, params[4]);
	embed.color = static_cast<int>(params[5]);
	embed.footerText = getAmxString(amx, params[6]);
	embed.footerIconUrl = getAmxString(amx, params[7]);
	embed.thumbnailUrl = getAmxString(amx, params[8]);
	embed.imageUrl = getAmxString(amx, params[9]);
	const cell handle = g_nextEmbedHandle++;
	g_embeds.emplace(handle, std::move(embed));
	return handle;
}

cell AMX_NATIVE_CALL Native_DeleteEmbed(AMX*, cell* params)
{
	return g_embeds.erase(params[1]) > 0 ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_AddEmbedField(AMX* amx, cell* params)
{
	auto it = g_embeds.find(params[1]);
	if (it == g_embeds.end()) return 0;
	DiscordBuilders::EmbedField field;
	field.name = getAmxString(amx, params[2]);
	field.value = getAmxString(amx, params[3]);
	field.inlineField = params[4] != 0;
	if (field.name.empty() || field.value.empty() || field.name.size() > 256 || field.value.size() > 1024 || it->second.fields.size() >= 25) return 0;
	it->second.fields.push_back(std::move(field));
	return 1;
}

cell AMX_NATIVE_CALL Native_SetEmbedTitle(AMX* amx, cell* params)
{
	auto it = g_embeds.find(params[1]); if (it == g_embeds.end()) return 0;
	it->second.title = getAmxString(amx, params[2]); return 1;
}

cell AMX_NATIVE_CALL Native_SetEmbedDescription(AMX* amx, cell* params)
{
	auto it = g_embeds.find(params[1]); if (it == g_embeds.end()) return 0;
	it->second.description = getAmxString(amx, params[2]); return 1;
}

cell AMX_NATIVE_CALL Native_SetEmbedUrl(AMX* amx, cell* params)
{
	auto it = g_embeds.find(params[1]); if (it == g_embeds.end()) return 0;
	it->second.url = getAmxString(amx, params[2]); return 1;
}

cell AMX_NATIVE_CALL Native_SetEmbedTimestamp(AMX* amx, cell* params)
{
	auto it = g_embeds.find(params[1]); if (it == g_embeds.end()) return 0;
	it->second.timestamp = getAmxString(amx, params[2]); return 1;
}

cell AMX_NATIVE_CALL Native_SetEmbedColor(AMX*, cell* params)
{
	auto it = g_embeds.find(params[1]); if (it == g_embeds.end()) return 0;
	it->second.color = static_cast<int>(params[2]); return 1;
}

cell AMX_NATIVE_CALL Native_SetEmbedFooter(AMX* amx, cell* params)
{
	auto it = g_embeds.find(params[1]); if (it == g_embeds.end()) return 0;
	it->second.footerText = getAmxString(amx, params[2]);
	it->second.footerIconUrl = getAmxString(amx, params[3]);
	return 1;
}

cell AMX_NATIVE_CALL Native_SetEmbedThumbnail(AMX* amx, cell* params)
{
	auto it = g_embeds.find(params[1]); if (it == g_embeds.end()) return 0;
	it->second.thumbnailUrl = getAmxString(amx, params[2]); return 1;
}

cell AMX_NATIVE_CALL Native_SetEmbedImage(AMX* amx, cell* params)
{
	auto it = g_embeds.find(params[1]); if (it == g_embeds.end()) return 0;
	it->second.imageUrl = getAmxString(amx, params[2]); return 1;
}

cell AMX_NATIVE_CALL Native_SendChannelEmbedMessage(AMX* amx, cell* params)
{
	const std::string channelId = channelIdForHandle(params[1]);
	const auto embedIt = g_embeds.find(params[2]);
	if (channelId.empty() || embedIt == g_embeds.end()) return 0;
	const std::string content = getAmxString(amx, params[3]);
	if (content.size() > 2000) return 0;
	std::shared_ptr<PreparedPawnCallback> callback;
	if (!capturePawnCallback(amx, params[4], params[5], params, 6, callback)) return 0;
	const std::string body = messagePayload(content, &embedIt->second);
	DiscordBot* bot = component() && component()->getBot() ? static_cast<DiscordBot*>(component()->getBot()) : nullptr;
	if (!bot) return 0;
	const bool queued = bot->submitRestTask([bot, channelId, body, callback](DiscordHTTP& rest)
	{
		const auto response = rest.sendMessagePayload(channelId, body);
		if (!callback) return;
		bot->enqueueCompletion([response, callback]()
		{
			completeMessageResponse(response.success, response.body, callback);
		});
	});
	if (queued) g_embeds.erase(embedIt);
	return queued ? 1 : 0;
}

// DCC passes only the caller's variadic arguments to async callbacks.  The
// result is available through its typed DCC_GetCreated* native during the call.
template <AMX_NATIVE Target, DccResult Kind>
cell AMX_NATIVE_CALL dccAsyncNative(AMX* amx, cell* params)
{
	ScopedDccValue<DccResult> capture(g_capturingDccResult, Kind);
	return Target(amx, params);
}

template <DccResult Kind>
cell AMX_NATIVE_CALL dccCreatedResult(AMX* amx, cell*)
{
	NativePawnScript* script = pawnScriptFor(amx);
	return script && script->GetID() == g_dccCallback.scriptId && g_dccCallback.kind == Kind
		? g_dccCallback.handle : 0;
}

cell AMX_NATIVE_CALL Native_DccCacheChannelMessage(AMX* amx, cell* params)
{
	const std::string channelId = getAmxString(amx, params[1]);
	const std::string messageId = getAmxString(amx, params[2]);
	if (!isDiscordSnowflake(channelId) || !isDiscordSnowflake(messageId)) return 0;
	if (!component() || component()->findMessageById(messageId)) return 0;
	std::shared_ptr<PreparedPawnCallback> callback;
	if (!capturePawnCallback(amx, params[3], params[4], params, 5, callback)) return 0;
	return submitAction("DCC_CacheChannelMessage", [channelId, messageId](DiscordHTTP& rest)
	{
		return rest.getMessage(channelId, messageId);
	}, [callback](const DiscordHTTP::Response& response)
	{
		completeMessageResponse(response.success, response.body, callback);
	}) ? 1 : 0;
}

void appendCoreNatives(std::vector<AMX_NATIVE_INFO>& natives)
{
	static const AMX_NATIVE_INFO kNatives[] = {
		{ "DBR_DCC_SendChannelMessage", dccAsyncNative<Native_SendChannelMessage, DccResult::Message> },
		{ "DBR_DCC_SendChannelEmbedMessage", dccAsyncNative<Native_SendChannelEmbedMessage, DccResult::Message> },
		{ "DBR_DCC_CreateGuildChannel", dccAsyncNative<Native_CreateGuildChannel, DccResult::GuildChannel> },
		{ "DBR_DCC_CreateGuildRole", dccAsyncNative<Native_CreateGuildRole, DccResult::GuildRole> },
		{ "DBR_DCC_CreatePrivateChannel", dccAsyncNative<Native_CreatePrivateChannel, DccResult::PrivateChannel> },
		{ "DBR_DCC_CacheChannelMessage", dccAsyncNative<Native_DccCacheChannelMessage, DccResult::Message> },
		{ "DBR_DCC_GetCreatedMessage", dccCreatedResult<DccResult::Message> },
		{ "DBR_DCC_GetCreatedGuildChannel", dccCreatedResult<DccResult::GuildChannel> },
		{ "DBR_DCC_GetCreatedGuildRole", dccCreatedResult<DccResult::GuildRole> },
		{ "DBR_DCC_GetCreatedPrivateChannel", dccCreatedResult<DccResult::PrivateChannel> },

		{ "DBR_ConnectBot", Native_ConnectDiscordBot },
		{ "DBR_IsConnected", Native_IsDiscordConnected },
		{ "DBR_DisconnectBot", Native_DisconnectBot },
		{ "DBR_SetDebugMode", Native_SetDebugMode },
		{ "DBR_IsDebugMode", Native_IsDebugMode },
		{ "DBR_SetTextEncoding", Native_SetTextEncoding },
		{ "DBR_GetTextEncoding", Native_GetTextEncoding },

		{ "DBR_FindChannelByID", Native_FindChannelById },
		{ "DBR_FindChannelByName", Native_FindChannelByName },
		{ "DBR_FindConfiguredChannel", Native_FindDiscordConfiguredChannel },
		{ "DBR_GetChannelID", Native_GetChannelId },
		{ "DBR_GetChannelName", Native_GetChannelName },
		{ "DBR_GetChannelTopic", Native_GetChannelTopic },
		{ "DBR_GetChannelType", Native_GetChannelType },
		{ "DBR_GetChannelGuild", Native_GetChannelGuild },
		{ "DBR_GetChannelPosition", Native_GetChannelPosition },
		{ "DBR_IsChannelNsfw", Native_IsChannelNsfw },
		{ "DBR_GetChannelParentCategory", Native_GetChannelParentCategory },
		{ "DBR_SendChannelMessage", Native_SendChannelMessage },
		{ "DBR_SetChannelName", Native_SetChannelName },
		{ "DBR_SetChannelTopic", Native_SetChannelTopic },
		{ "DBR_SetChannelPosition", Native_SetChannelPosition },
		{ "DBR_SetChannelNsfw", Native_SetChannelNsfw },
		{ "DBR_SetChannelParentCategory", Native_SetChannelParentCategory },
		{ "DBR_DeleteChannel", Native_DeleteChannel },

		{ "DBR_FindUserByID", Native_FindUserById },
		{ "DBR_FindUserByName", Native_FindUserByName },
		{ "DBR_GetUserID", Native_GetUserId },
		{ "DBR_GetUserName", Native_GetUserName },
		{ "DBR_GetUserDiscriminator", Native_GetUserDiscriminator },
		{ "DBR_IsUserBot", Native_IsUserBot },
		{ "DBR_IsUserVerified", Native_IsUserVerified },

		{ "DBR_FindGuildByID", Native_FindGuildById },
		{ "DBR_FindGuildByName", Native_FindGuildByName },
		{ "DBR_GetGuildID", Native_GetGuildId },
		{ "DBR_GetGuildName", Native_GetGuildName },
		{ "DBR_GetGuildOwnerID", Native_GetGuildOwnerId },
		{ "DBR_GetGuildRole", Native_GetGuildRole },
		{ "DBR_GetGuildRoleCount", Native_GetGuildRoleCount },
		{ "DBR_GetGuildMember", Native_GetGuildMember },
		{ "DBR_GetGuildMemberCount", Native_GetGuildMemberCount },
		{ "DBR_GetGuildMemberVoiceChannel", Native_GetGuildMemberVoiceChannel },
		{ "DBR_GetGuildMemberNickname", Native_GetGuildMemberNickname },
		{ "DBR_GetGuildMemberRole", Native_GetGuildMemberRole },
		{ "DBR_GetGuildMemberRoleCount", Native_GetGuildMemberRoleCount },
		{ "DBR_HasGuildMemberRole", Native_HasGuildMemberRole },
		{ "DBR_GetGuildMemberStatus", Native_GetGuildMemberStatus },
		{ "DBR_GetGuildChannel", Native_GetGuildChannel },
		{ "DBR_GetGuildChannelCount", Native_GetGuildChannelCount },
		{ "DBR_GetAllGuilds", Native_GetAllGuilds },
		{ "DBR_SetGuildName", Native_SetGuildName },
		{ "DBR_CreateGuildChannel", Native_CreateGuildChannel },

		{ "DBR_FindRoleByID", Native_FindRoleById },
		{ "DBR_FindRoleByName", Native_FindRoleByName },
		{ "DBR_GetRoleID", Native_GetRoleId },
		{ "DBR_GetRoleName", Native_GetRoleName },
		{ "DBR_GetRoleColour", Native_GetRoleColor },
		{ "DBR_GetRolePermissions", Native_GetRolePermissions },
		{ "DBR_IsRoleHoist", Native_IsRoleHoist },
		{ "DBR_GetRolePosition", Native_GetRolePosition },
		{ "DBR_IsRoleMentionable", Native_IsRoleMentionable },
		{ "DBR_SetGuildRolePosition", Native_SetGuildRolePosition },
		{ "DBR_SetGuildRoleName", Native_SetGuildRoleName },
		{ "DBR_SetGuildRolePermissions", Native_SetGuildRolePermissions },
		{ "DBR_SetGuildRoleColour", Native_SetGuildRoleColor },
		{ "DBR_SetGuildRoleHoist", Native_SetGuildRoleHoist },
		{ "DBR_SetGuildRoleMentionable", Native_SetGuildRoleMentionable },
		{ "DBR_CreateGuildRole", Native_CreateGuildRole },
		{ "DBR_DeleteGuildRole", Native_DeleteGuildRole },

		{ "DBR_GetMessageID", Native_GetMessageId },
		{ "DBR_GetMessageChannel", Native_GetMessageChannel },
		{ "DBR_GetMessageAuthor", Native_GetMessageAuthor },
		{ "DBR_GetMessageContent", Native_GetMessageContent },
		{ "DBR_IsMessageTTS", Native_IsMessageTts },
		{ "DBR_IsMessageMentioningEveryone", Native_IsMessageMentioningEveryone },
		{ "DBR_GetMessageUserMentionCount", Native_GetMessageUserMentionCount },
		{ "DBR_GetMessageUserMention", Native_GetMessageUserMention },
		{ "DBR_GetMessageRoleMentionCount", Native_GetMessageRoleMentionCount },
		{ "DBR_GetMessageRoleMention", Native_GetMessageRoleMention },
		{ "DBR_ForgetMessage", Native_DeleteInternalMessage },
		{ "DBR_SetMessagePersistent", Native_SetMessagePersistent },
		{ "DBR_DeleteMessage", Native_DeleteMessage },
		{ "DBR_EditMessage", Native_EditMessage },

		{ "DBR_CreateEmoji", Native_CreateEmoji },
		{ "DBR_DeleteEmoji", Native_DeleteEmoji },
		{ "DBR_GetEmojiName", Native_GetEmojiName },
		{ "DBR_CreateReaction", Native_CreateReaction },
		{ "DBR_DeleteMessageReaction", Native_DeleteMessageReaction },

		{ "DBR_TriggerBotTypingIndicator", Native_TriggerBotTypingIndicator },
		{ "DBR_CreatePrivateChannel", Native_CreatePrivateChannel },
		{ "DBR_EscapeMarkdown", Native_EscapeMarkdown },

		{ "DBR_CreateEmbed", Native_CreateEmbed },
		{ "DBR_DeleteEmbed", Native_DeleteEmbed },
		{ "DBR_SendChannelEmbedMessage", Native_SendChannelEmbedMessage },
		{ "DBR_AddEmbedField", Native_AddEmbedField },
		{ "DBR_SetEmbedTitle", Native_SetEmbedTitle },
		{ "DBR_SetEmbedDescription", Native_SetEmbedDescription },
		{ "DBR_SetEmbedURL", Native_SetEmbedUrl },
		{ "DBR_SetEmbedTimestamp", Native_SetEmbedTimestamp },
		{ "DBR_SetEmbedColour", Native_SetEmbedColor },
		{ "DBR_SetEmbedFooter", Native_SetEmbedFooter },
		{ "DBR_SetEmbedThumbnail", Native_SetEmbedThumbnail },
		{ "DBR_SetEmbedImage", Native_SetEmbedImage },
	};
	natives.insert(natives.end(), std::begin(kNatives), std::end(kNatives));
}
}

using namespace DiscordNatives;

namespace
{
constexpr std::size_t MAX_GUARDED_NATIVES = 512;
AMX_NATIVE g_nativeTargets[MAX_GUARDED_NATIVES] {};
const char* g_nativeNames[MAX_GUARDED_NATIVES] {};

template <std::size_t Index>
cell AMX_NATIVE_CALL guardedNative(AMX* amx, cell* params)
{
	try
	{
		return g_nativeTargets[Index](amx, params);
	}
	catch (const std::exception& exception)
	{
		logWarning(std::string("[DiscordBridge] ") + g_nativeNames[Index] + " failed: " + exception.what());
	}
	catch (...)
	{
		logWarning(std::string("[DiscordBridge] ") + g_nativeNames[Index] + " failed with an unknown exception");
	}
	return 0;
}

template <std::size_t... Indexes>
constexpr std::array<AMX_NATIVE, sizeof...(Indexes)> makeGuardedNatives(std::index_sequence<Indexes...>)
{
	return { { &guardedNative<Indexes>... } };
}

constexpr auto kGuardedNatives = makeGuardedNatives(std::make_index_sequence<MAX_GUARDED_NATIVES>());
}

int RegisterDiscordNatives(IPawnScript& script)
{
	static const std::vector<AMX_NATIVE_INFO> kNativeList = []()
	{
		std::vector<AMX_NATIVE_INFO> natives;
		natives.reserve(360);
		appendCoreNatives(natives);
		appendInteractionNatives(natives);
		appendManagementNatives(natives);
		// An exception must never unwind through the AMX's C code, so each
		// native runs inside a guard that logs it and returns 0 instead.
		const std::size_t guarded = std::min(natives.size(), MAX_GUARDED_NATIVES);
		for (std::size_t index = 0; index < guarded; ++index)
		{
			g_nativeTargets[index] = natives[index].func;
			g_nativeNames[index] = natives[index].name;
			natives[index].func = kGuardedNatives[index];
		}
		if (natives.size() > MAX_GUARDED_NATIVES)
		{
			logWarning("[DiscordBridge] more natives than exception guards; raise MAX_GUARDED_NATIVES");
		}
		return natives;
	}();
	return script.Register(kNativeList.data(), static_cast<int>(kNativeList.size()));
}

void ResetDiscordNativeHandles()
{
	resetNativeHandles();
}

void ForgetDiscordNativeScript(IPawnScript& script)
{
	forgetInteractionScript(script.GetID());
}

void ServiceDiscordNatives()
{
	serviceInteractionState();
}

void NotifyDiscordNativesReady()
{
	onInteractionBotReady();
}

void NotifyDiscordNativesDisconnected()
{
	onInteractionBotDisconnected();
}

cell GetOrCreateDiscordChannelHandle(StringView channelId)
{
	return assignChannelHandle(channelId);
}

cell GetOrCreateDiscordGuildHandle(StringView guildId)
{
	return assignGuildHandle(guildId);
}

cell GetOrCreateDiscordUserHandle(StringView userId)
{
	return assignUserHandle(userId);
}

cell GetOrCreateDiscordMessageHandle(StringView messageId, StringView channelId)
{
	const cell handle = assignMessageHandle(messageId);
	rememberMessageChannel(handle, channelId);
	return handle;
}

cell GetOrCreateDiscordRoleHandle(StringView roleId)
{
	return assignRoleHandle(roleId);
}

cell GetOrCreateDiscordEmojiHandle(StringView name, StringView snowflake)
{
	std::string token(name.data(), name.length());
	if (!snowflake.empty())
	{
		token += ":";
		token.append(snowflake.data(), snowflake.length());
	}
	return assignEmojiHandle(token);
}

cell CreateDiscordEmojiHandle(StringView name, StringView snowflake)
{
	std::string token(name.data(), name.length());
	if (!snowflake.empty())
	{
		token += ":";
		token.append(snowflake.data(), snowflake.length());
	}
	if (token.empty()) return 0;

	const cell handle = g_nextEmojiHandle++;
	g_emojiHandleToToken.emplace(handle, std::move(token));
	return handle;
}

void DeleteDiscordEmojiHandle(cell handle)
{
	if (handle != 0) g_emojiHandleToToken.erase(handle);
}
