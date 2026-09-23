/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

// Application commands, the interaction dispatcher and the payload builders
// exposed to Pawn: message components (classic and V2), message builders and
// modals.

#include "discord-mentions.hpp"
#include "natives-internal.hpp"
#include "discord-bot.hpp"
#include "discord-component.hpp"
#include "discord-guild.hpp"
#include "discord-http.hpp"
#include "discord-json.hpp"
#include "discord-message.hpp"
#include "discord-role.hpp"
#include "discord-user.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>

namespace DiscordNatives
{
namespace
{
using namespace DiscordBuilders;
using Clock = std::chrono::steady_clock;

enum class ResponseState
{
	None,
	Deferred,
	DeferredUpdate,
	Replied,
	Modal,
	Autocomplete
};

enum InteractionType : int
{
	PingInteraction = 1,
	CommandInteraction = 2,
	ComponentInteraction = 3,
	AutocompleteInteraction = 4,
	ModalSubmitInteraction = 5
};

struct InteractionState
{
	std::vector<std::string> mentionedUserIds;
	std::string id;
	std::string token;
	int type = 0;
	DiscordJson data = DiscordJson::object();
	std::string channelId;
	std::string guildId;
	std::string userId;
	std::string messageId;
	std::string locale;
	uint64_t permissions = 0;
	ResponseState response = ResponseState::None;
	DiscordJson autocompleteChoices = DiscordJson::array();
	Clock::time_point receivedAt;
};

struct InteractionHandler
{
	int type = 0;
	std::string key;
	std::string callback;
	int scriptId = -1;
	bool prefix = false;
};

constexpr size_t MAX_STORED_INTERACTIONS = 512;
constexpr size_t MAX_BUILDERS = 4096;
constexpr size_t MAX_HANDLERS = 1024;
// Interaction tokens stay valid for 15 minutes.
constexpr auto INTERACTION_LIFETIME = std::chrono::minutes(15);
// Commands are usually created in bursts during startup; publish once the
// burst settles.
constexpr auto COMMAND_DEPLOY_DELAY = std::chrono::seconds(2);
constexpr uint64_t PERMISSION_ADMINISTRATOR = 1ULL << 3;

std::map<cell, InteractionState> g_interactions;
cell g_nextInteraction = 1;
ComponentStore g_components;
std::map<cell, MessageBuilder> g_builders;
cell g_nextBuilder = 1;
std::map<cell, Modal> g_modals;
cell g_nextModal = 1;
CommandStore g_commands;
std::vector<InteractionHandler> g_handlers;
bool g_autoDefer = true;
bool g_commandAutoDeploy = true;
bool g_commandsDirty = false;
bool g_commandsEverCreated = false;
Clock::time_point g_commandsChangedAt;
// Last JSON published per scope ("" is the global scope).  Identical
// redeployments are skipped so gamemode restarts do not hit Discord.
std::map<std::string, std::string> g_deployedScopes;
// JSON currently being published per scope, so the same content is never
// sent twice while the first request is still in flight.
std::map<std::string, std::string> g_pendingScopes;
// Warn once when commands keep waiting for a bot that never becomes ready.
constexpr auto COMMAND_WAIT_WARNING_DELAY = std::chrono::seconds(30);
bool g_commandWaitWarned = false;

bool hasParams(const cell* params, size_t count)
{
	return nativeParamCount(params) >= count;
}

cell writeString(AMX* amx, cell address, cell size, const std::string& value)
{
	return setAmxString(amx, address, value, size) ? 1 : 0;
}

bool writeCell(AMX* amx, cell address, cell value)
{
	cell* out = nativeRef(amx, address);
	if (!out) return false;
	*out = value;
	return true;
}

void warnNative(const char* native, const std::string& error)
{
	logWarning(std::string("[DiscordBridge] ") + native + ": " + error);
}

InteractionState* interactionFor(cell handle)
{
	const auto it = g_interactions.find(handle);
	return it == g_interactions.end() ? nullptr : &it->second;
}

void forgetInteractionMessage(const InteractionState& state)
{
	DiscordBridgeComponent* bridge = component();
	if (!bridge || state.messageId.empty()) return;
	if (auto* message = static_cast<DiscordMessage*>(bridge->findMessageById(state.messageId)); message && !message->isPersistent())
	{
		bridge->removeMessage(state.messageId);
	}
}

void markCommandsChanged()
{
	g_commandsDirty = true;
	g_commandsChangedAt = Clock::now();
}

// ---------------------------------------------------------------------------
// Interaction responses
// ---------------------------------------------------------------------------

bool sendInitialResponse(InteractionState& state, int callbackType, DiscordJson data, const char* action)
{
	DiscordJson body = { { "type", callbackType } };
	if (!data.is_null()) body["data"] = std::move(data);
	return submitAction(action, [id = state.id, token = state.token, payload = body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)](DiscordHTTP& rest)
	{
		return rest.createInteractionResponse(id, token, payload);
	});
}

bool sendWebhookRequest(const InteractionState& state, http::verb method, const std::string& suffix,
	const std::string& body, const char* action)
{
	return submitAction(action, [token = state.token, method, suffix, body](DiscordHTTP& rest)
	{
		return rest.request(method, "/webhooks/" + rest.getApplicationId() + "/" + token + suffix, body);
	});
}

bool deferInteraction(InteractionState& state, bool ephemeral)
{
	if (state.response != ResponseState::None || state.type == AutocompleteInteraction) return false;
	DiscordJson data = nullptr;
	if (ephemeral) data = { { "flags", MESSAGE_FLAG_EPHEMERAL } };
	if (!sendInitialResponse(state, 5, std::move(data), "DBR_DeferInteraction")) return false;
	state.response = ResponseState::Deferred;
	return true;
}

bool deferInteractionUpdate(InteractionState& state)
{
	if (state.response != ResponseState::None) return false;
	if (state.type != ComponentInteraction && state.type != ModalSubmitInteraction) return false;
	if (!sendInitialResponse(state, 6, nullptr, "DBR_DeferInteractionUpdate")) return false;
	state.response = ResponseState::DeferredUpdate;
	return true;
}

bool sendAutocomplete(InteractionState& state)
{
	if (state.type != AutocompleteInteraction || state.response != ResponseState::None) return false;
	if (!sendInitialResponse(state, 8, { { "choices", state.autocompleteChoices } }, "DBR_SendAutocomplete")) return false;
	state.response = ResponseState::Autocomplete;
	return true;
}

// Sends a message as the best response for the interaction's current state:
// the initial reply, the edit of a deferred "thinking" reply, or a follow-up.
bool respondWithMessage(InteractionState& state, DiscordJson message, bool ephemeral)
{
	switch (state.response)
	{
		case ResponseState::None:
			if (state.type == AutocompleteInteraction) return false;
			if (!sendInitialResponse(state, 4, std::move(message), "DBR_RespondInteraction")) return false;
			state.response = ResponseState::Replied;
			return true;
		case ResponseState::Deferred:
			// The deferral already decided whether the reply is ephemeral.
			if (auto flags = message.find("flags"); flags != message.end())
			{
				const int value = flags->get<int>() & ~MESSAGE_FLAG_EPHEMERAL;
				if (value == 0) message.erase("flags");
				else message["flags"] = value;
			}
			if (!sendWebhookRequest(state, http::verb::patch, "/messages/@original", message.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), "DBR_RespondInteraction")) return false;
			state.response = ResponseState::Replied;
			return true;
		case ResponseState::DeferredUpdate:
		case ResponseState::Replied:
			if (ephemeral) message["flags"] = jsonInt(message, "flags", 0) | MESSAGE_FLAG_EPHEMERAL;
			return sendWebhookRequest(state, http::verb::post, "", message.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), "DBR_SendInteractionFollowup");
		default:
			return false;
	}
}

void finalizeInteraction(cell handle)
{
	InteractionState* state = interactionFor(handle);
	if (!state || state->response != ResponseState::None) return;
	if (state->type == AutocompleteInteraction)
	{
		sendAutocomplete(*state);
		return;
	}
	if (!g_autoDefer) return;
	if (state->type == ComponentInteraction) deferInteractionUpdate(*state);
	else deferInteraction(*state, false);
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

void destroyBuilder(cell handle)
{
	const auto it = g_builders.find(handle);
	if (it == g_builders.end()) return;
	for (const Handle component : it->second.components) g_components.destroy(component);
	g_builders.erase(it);
}

void destroyModal(cell handle)
{
	const auto it = g_modals.find(handle);
	if (it == g_modals.end()) return;
	for (const Handle component : it->second.components) g_components.destroy(component);
	g_modals.erase(it);
}

// Renders and consumes a message builder.  The builder is destroyed even when
// rendering fails so scripts never leak a builder on an error path.
bool takeBuilderPayload(cell handle, bool ephemeral, DiscordJson& out, const char* native)
{
	const auto it = g_builders.find(handle);
	if (it == g_builders.end()) return false;
	std::string error;
	const bool rendered = renderMessage(it->second, g_components, ephemeral, out, error);
	if (!rendered) warnNative(native, error);
	destroyBuilder(handle);
	return rendered;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool deployCommands()
{
	DiscordBot* bot = nativeBot();
	if (!bot || !bot->isConnected()) return false;
	g_commandsDirty = false;

	std::vector<std::string> scopes = g_commands.scopes();
	for (const auto& entry : g_deployedScopes)
	{
		if (std::find(scopes.begin(), scopes.end(), entry.first) == scopes.end()) scopes.push_back(entry.first);
	}

	bool queued = true;
	for (const std::string& scope : scopes)
	{
		const DiscordJson commands = g_commands.renderScope(scope);
		const std::string body = commands.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
		const auto pending = g_pendingScopes.find(scope);
		if (pending != g_pendingScopes.end() && pending->second == body) continue;
		const auto deployed = g_deployedScopes.find(scope);
		if (pending == g_pendingScopes.end() && deployed != g_deployedScopes.end() && deployed->second == body) continue;
		if (deployed == g_deployedScopes.end() && commands.empty()) continue;

		const std::string label = scope.empty() ? std::string("global scope") : "guild " + scope;
		const size_t count = commands.size();
		std::string names;
		for (const auto& command : commands)
		{
			if (!names.empty()) names += ", ";
			names += jsonString(command, "name");
		}
		logInfo("[DiscordBridge] publishing " + std::to_string(count) + " application command(s) to the " + label +
			(names.empty() ? std::string() : ": " + names));
		g_pendingScopes[scope] = body;
		const bool submitted = submitAction("DBR_DeployCommands", [scope, body](DiscordHTTP& rest)
		{
			const std::string& application = rest.getApplicationId();
			const std::string endpoint = scope.empty()
				? "/applications/" + application + "/commands"
				: "/applications/" + application + "/guilds/" + scope + "/commands";
			return rest.request(http::verb::put, endpoint, body);
		}, [scope, body, label, count](const DiscordHTTP::Response& response)
		{
			if (auto current = g_pendingScopes.find(scope); current != g_pendingScopes.end() && current->second == body)
			{
				g_pendingScopes.erase(current);
			}
			if (!response.success) return;
			if (count == 0) g_deployedScopes.erase(scope);
			else g_deployedScopes[scope] = body;
			logInfo("[DiscordBridge] published " + std::to_string(count) + " application command(s) to the " + label);
		});
		if (!submitted) g_pendingScopes.erase(scope);
		queued = submitted && queued;
	}
	return queued;
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

void cacheResolvedEntities(InteractionState& state)
{
	DiscordBridgeComponent* bridge = component();
	const auto resolvedIt = state.data.find("resolved");
	if (!bridge || resolvedIt == state.data.end() || !resolvedIt->is_object()) return;
	const DiscordJson& resolved = *resolvedIt;

	auto section = [&resolved](const char* key) -> const DiscordJson*
	{
		const auto it = resolved.find(key);
		return it != resolved.end() && it->is_object() ? &*it : nullptr;
	};

	if (const DiscordJson* users = section("users"))
	{
		for (const auto& user : *users)
		{
			if (user.is_object()) bridge->upsertUserFromJson(user.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
		}
	}
	DiscordGuild* guild = ensureGuildCached(state.guildId);
	if (const DiscordJson* members = section("members"); members && guild)
	{
		for (auto it = members->begin(); it != members->end(); ++it)
		{
			if (it.value().is_object()) guild->updateMemberFromJson(it.value().dump(-1, ' ', false, DiscordJson::error_handler_t::replace), it.key());
		}
	}
	if (const DiscordJson* roles = section("roles"))
	{
		for (const auto& role : *roles)
		{
			if (role.is_object()) bridge->upsertRoleFromJson(role.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), state.guildId);
		}
	}
	if (const DiscordJson* messages = section("messages"))
	{
		for (const auto& message : *messages)
		{
			if (!message.is_object()) continue;
			if (DiscordMessage* cached = bridge->upsertMessageFromJson(message.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)))
			{
				state.messageId.assign(cached->getMessageId().data(), cached->getMessageId().length());
				rememberMessageChannel(assignMessageHandle(cached->getMessageId()), cached->getChannelId());
			}
		}
	}
}

bool callCommandCallback(const InteractionState& state, cell handle, cell userHandle)
{
	const std::string name = jsonString(state.data, "name");
	const int type = jsonInt(state.data, "type", static_cast<int>(ChatInputCommand));
	Handle command = g_commands.findCommand(name, type, state.guildId);
	if (!command) command = g_commands.findCommand(name, type, std::string());
	const Command* registered = g_commands.getCommand(command);
	if (!registered || registered->callback.empty()) return false;

	NativePawnScript* script = pawnScriptForId(registered->ownerScriptId);
	cell result = 1;
	if (!callPawnPublicOnScript(script, registered->callback.c_str(), result, handle, userHandle))
	{
		logWarning("[DiscordBridge] command '" + name + "' callback '" + registered->callback + "' is not loaded");
		return false;
	}
	return true;
}

bool callRegisteredHandler(int type, const std::string& key, cell handle, cell userHandle)
{
	const InteractionHandler* best = nullptr;
	for (const auto& handler : g_handlers)
	{
		if (handler.type != type) continue;
		if (!handler.prefix && handler.key == key) { best = &handler; break; }
		if (handler.prefix && key.rfind(handler.key, 0) == 0 && (!best || handler.key.size() > best->key.size())) best = &handler;
	}
	if (!best) return false;
	const std::string callback = best->callback;
	cell result = 1;
	return callPawnPublicOnScript(pawnScriptForId(best->scriptId), callback.c_str(), result, handle, userHandle, StringView(toPawnText(key)));
}

void dispatchInteraction(const std::string& json)
{
	const DiscordJson payload = DiscordJson::parse(json, nullptr, false);
	DiscordBridgeComponent* bridge = component();
	if (!bridge || payload.is_discarded() || !payload.is_object()) return;

	InteractionState state;
	state.id = jsonString(payload, "id");
	state.token = jsonString(payload, "token");
	state.type = jsonInt(payload, "type", 0);
	state.receivedAt = Clock::now();
	if (state.id.empty() || state.token.empty()) return;
	if (state.type == PingInteraction)
	{
		sendInitialResponse(state, 1, nullptr, "interaction ping");
		return;
	}
	if (state.type < CommandInteraction || state.type > ModalSubmitInteraction) return;

	if (auto data = payload.find("data"); data != payload.end() && data->is_object()) state.data = *data;
	state.channelId = jsonString(payload, "channel_id");
	if (auto guild = payload.find("guild_id"); guild != payload.end() && guild->is_string()) state.guildId = guild->get<std::string>();
	if (auto locale = payload.find("locale"); locale != payload.end() && locale->is_string()) state.locale = locale->get<std::string>();

	const auto memberIt = payload.find("member");
	const bool hasMember = memberIt != payload.end() && memberIt->is_object();
	DiscordJson userJson = nullptr;
	if (hasMember && memberIt->find("user") != memberIt->end()) userJson = (*memberIt)["user"];
	else if (auto user = payload.find("user"); user != payload.end()) userJson = *user;

	DiscordUser* user = userJson.is_object() ? bridge->upsertUserFromJson(userJson.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)) : nullptr;
	if (user) state.userId.assign(user->getUserId().data(), user->getUserId().length());
	if (hasMember)
	{
		if (auto permissions = memberIt->find("permissions"); permissions != memberIt->end() && permissions->is_string())
		{
			try { state.permissions = std::stoull(permissions->get<std::string>()); } catch (...) { state.permissions = 0; }
		}
		if (DiscordGuild* guild = ensureGuildCached(state.guildId); guild && !state.userId.empty())
		{
			guild->updateMemberFromJson(memberIt->dump(), state.userId);
		}
	}
	cacheResolvedEntities(state);
	if (auto options = state.data.find("options"); options != state.data.end())
		DiscordMentions::collect(*options, state.mentionedUserIds);
	if (auto message = payload.find("message"); message != payload.end() && message->is_object())
	{
		if (DiscordMessage* cached = bridge->upsertMessageFromJson(message->dump()))
		{
			state.messageId.assign(cached->getMessageId().data(), cached->getMessageId().length());
			rememberMessageChannel(assignMessageHandle(cached->getMessageId()), cached->getChannelId());
		}
	}

	while (g_interactions.size() >= MAX_STORED_INTERACTIONS)
	{
		forgetInteractionMessage(g_interactions.begin()->second);
		g_interactions.erase(g_interactions.begin());
	}
	const cell handle = g_nextInteraction++;
	const int type = state.type;
	const std::string commandName = jsonString(state.data, "name");
	const std::string customId = jsonString(state.data, "custom_id");
	const int componentType = jsonInt(state.data, "component_type", 0);
	std::string focusedOption;
	if (type == AutocompleteInteraction)
	{
		if (const DiscordJson* focused = findFocusedOption(state.data)) focusedOption = focused->value("name", std::string());
	}
	g_interactions.emplace(handle, std::move(state));
	const cell userHandle = user ? assignUserHandle(user->getUserId()) : 0;

	// DBR_OnInteraction sees every interaction first; returning 0 stops the
	// specific handlers, which makes it a natural place for permission gates.
	bool handled = callPawnPublic("DBR_OnInteraction", 1, handle, userHandle, static_cast<cell>(type)) == 0;
	const std::string key = (type == CommandInteraction || type == AutocompleteInteraction) ? commandName : customId;
	if (!handled && type == CommandInteraction) handled = callCommandCallback(g_interactions[handle], handle, userHandle);
	if (!handled) handled = callRegisteredHandler(type, key, handle, userHandle);
	if (!handled && interactionFor(handle))
	{
		switch (type)
		{
			case CommandInteraction:
				callPawnPublic("DBR_OnCommand", 1, handle, userHandle, StringView(toPawnText(commandName)));
				break;
			case ComponentInteraction:
				if (componentType == Button) callPawnPublic("DBR_OnButton", 1, handle, userHandle, StringView(toPawnText(customId)));
				else callPawnPublic("DBR_OnSelectMenu", 1, handle, userHandle, StringView(toPawnText(customId)));
				break;
			case AutocompleteInteraction:
				callPawnPublic("DBR_OnAutocomplete", 1, handle, userHandle, StringView(toPawnText(commandName)), StringView(toPawnText(focusedOption)));
				break;
			case ModalSubmitInteraction:
				callPawnPublic("DBR_OnModalSubmit", 1, handle, userHandle, StringView(toPawnText(customId)));
				break;
			default:
				break;
		}
	}
	finalizeInteraction(handle);
}

// ---------------------------------------------------------------------------
// Interaction information natives
// ---------------------------------------------------------------------------

cell AMX_NATIVE_CALL Native_GetInteractionType(AMX*, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	return state ? static_cast<cell>(state->type) : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionId(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	return state && hasParams(params, 3) ? writeString(amx, params[2], params[3], state->id) : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionUser(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || state->userId.empty()) return 0;
	return writeCell(amx, params[2], assignUserHandle(state->userId)) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionChannel(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state) return 0;
	cell channel = 0;
	if (!state->channelId.empty())
	{
		channel = assignChannelHandle(state->channelId);
		rememberChannelGuild(channel, state->guildId);
	}
	return writeCell(amx, params[2], channel) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionGuild(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state) return 0;
	return writeCell(amx, params[2], state->guildId.empty() ? 0 : assignGuildHandle(state->guildId)) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionMessage(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || state->messageId.empty()) return 0;
	return writeCell(amx, params[2], assignMessageHandle(state->messageId)) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionLocale(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	return state && hasParams(params, 3) ? writeString(amx, params[2], params[3], state->locale) : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionCommandName(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || (state->type != CommandInteraction && state->type != AutocompleteInteraction)) return 0;
	return hasParams(params, 3) ? writeString(amx, params[2], params[3], jsonString(state->data, "name")) : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionCommandType(AMX*, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || (state->type != CommandInteraction && state->type != AutocompleteInteraction)) return 0;
	return static_cast<cell>(jsonInt(state->data, "type", static_cast<int>(ChatInputCommand)));
}

cell AMX_NATIVE_CALL Native_GetInteractionSubcommand(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 3)) return 0;
	return writeString(amx, params[2], params[3], findSubcommand(state->data, false));
}

cell AMX_NATIVE_CALL Native_GetInteractionSubcommandGroup(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 3)) return 0;
	return writeString(amx, params[2], params[3], findSubcommand(state->data, true));
}

cell AMX_NATIVE_CALL Native_GetInteractionTargetId(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 3)) return 0;
	const std::string target = jsonString(state->data, "target_id");
	if (target.empty()) return 0;
	return writeString(amx, params[2], params[3], target);
}

cell AMX_NATIVE_CALL Native_GetInteractionCustomId(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 3)) return 0;
	return writeString(amx, params[2], params[3], jsonString(state->data, "custom_id"));
}

cell AMX_NATIVE_CALL Native_GetInteractionComponentType(AMX*, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	return state && state->type == ComponentInteraction ? static_cast<cell>(jsonInt(state->data, "component_type", 0)) : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionValueCount(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 2)) return 0;
	std::vector<std::string> values;
	collectSubmittedValues(state->data, getAmxString(amx, params[2]), values);
	return static_cast<cell>(values.size());
}

cell AMX_NATIVE_CALL Native_GetInteractionValue(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 5) || params[5] < 0) return 0;
	std::vector<std::string> values;
	collectSubmittedValues(state->data, getAmxString(amx, params[2]), values);
	const size_t index = static_cast<size_t>(params[5]);
	if (index >= values.size()) return 0;
	return writeString(amx, params[3], params[4], values[index]);
}

const DiscordJson* optionFor(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || (state->type != CommandInteraction && state->type != AutocompleteInteraction)) return nullptr;
	return findCommandOption(state->data, getAmxString(amx, params[2]));
}

const DiscordJson* optionValue(AMX* amx, cell* params)
{
	const DiscordJson* option = optionFor(amx, params);
	if (!option) return nullptr;
	const auto value = option->find("value");
	return value == option->end() ? nullptr : &*value;
}

cell AMX_NATIVE_CALL Native_HasInteractionOption(AMX* amx, cell* params)
{
	return optionValue(amx, params) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionOptionType(AMX* amx, cell* params)
{
	const DiscordJson* option = optionFor(amx, params);
	return option ? static_cast<cell>(option->value("type", 0)) : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionMentionCount(AMX* amx, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	const InteractionState* state = interactionFor(params[1]);
	return state && writeCell(amx, params[2], static_cast<cell>(state->mentionedUserIds.size())) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionMention(AMX* amx, cell* params)
{
	if (!hasParams(params, 3) || params[2] < 0) return 0;
	const InteractionState* state = interactionFor(params[1]);
	if (!state || static_cast<size_t>(params[2]) >= state->mentionedUserIds.size()) return 0;
	const cell user = assignUserHandle(state->mentionedUserIds[static_cast<size_t>(params[2])]);
	return user && writeCell(amx, params[3], user) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionOptionString(AMX* amx, cell* params)
{
	const DiscordJson* value = hasParams(params, 4) ? optionValue(amx, params) : nullptr;
	return value ? writeString(amx, params[3], params[4], jsonScalarToString(*value)) : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionOptionInt(AMX* amx, cell* params)
{
	const DiscordJson* value = hasParams(params, 3) ? optionValue(amx, params) : nullptr;
	if (!value) return 0;
	long long number = 0;
	if (value->is_number()) number = value->is_number_float() ? static_cast<long long>(value->get<double>()) : value->get<long long>();
	else if (value->is_string())
	{
		// Autocomplete sends the partially typed value as a string.
		try { number = std::stoll(value->get<std::string>()); } catch (...) { return 0; }
	}
	else return 0;
	return writeCell(amx, params[3], static_cast<cell>(number)) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionOptionFloat(AMX* amx, cell* params)
{
	const DiscordJson* value = hasParams(params, 3) ? optionValue(amx, params) : nullptr;
	if (!value) return 0;
	float number = 0.0f;
	if (value->is_number()) number = static_cast<float>(value->get<double>());
	else if (value->is_string())
	{
		try { number = std::stof(value->get<std::string>()); } catch (...) { return 0; }
	}
	else return 0;
	return writeCell(amx, params[3], amx_ftoc(number)) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionOptionBool(AMX* amx, cell* params)
{
	const DiscordJson* value = hasParams(params, 3) ? optionValue(amx, params) : nullptr;
	if (!value || !value->is_boolean()) return 0;
	return writeCell(amx, params[3], value->get<bool>() ? 1 : 0) ? 1 : 0;
}

cell entityOption(AMX* amx, cell* params, cell (*assign)(StringView))
{
	const DiscordJson* value = hasParams(params, 3) ? optionValue(amx, params) : nullptr;
	if (!value || !value->is_string() || !isDiscordSnowflake(value->get<std::string>())) return 0;
	return writeCell(amx, params[3], assign(value->get<std::string>())) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionOptionUser(AMX* amx, cell* params)
{
	return entityOption(amx, params, assignUserHandle);
}

cell AMX_NATIVE_CALL Native_GetInteractionOptionChannel(AMX* amx, cell* params)
{
	const cell result = entityOption(amx, params, assignChannelHandle);
	if (result)
	{
		const InteractionState* state = interactionFor(params[1]);
		const DiscordJson* value = optionValue(amx, params);
		if (state && value) rememberChannelGuild(assignChannelHandle(value->get<std::string>()), state->guildId);
	}
	return result;
}

cell AMX_NATIVE_CALL Native_GetInteractionOptionRole(AMX* amx, cell* params)
{
	return entityOption(amx, params, assignRoleHandle);
}

cell AMX_NATIVE_CALL Native_GetInteractionFocusedOption(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || state->type != AutocompleteInteraction || !hasParams(params, 3)) return 0;
	const DiscordJson* focused = findFocusedOption(state->data);
	return focused ? writeString(amx, params[2], params[3], focused->value("name", std::string())) : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionAttachmentUrl(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 4)) return 0;
	const std::string id = getAmxString(amx, params[2]);
	const auto resolved = state->data.find("resolved");
	if (resolved == state->data.end() || !resolved->is_object()) return 0;
	const auto attachments = resolved->find("attachments");
	if (attachments == resolved->end() || !attachments->is_object()) return 0;
	const auto attachment = attachments->find(id);
	if (attachment == attachments->end() || !attachment->is_object()) return 0;
	return writeString(amx, params[3], params[4], attachment->value("url", std::string()));
}

cell AMX_NATIVE_CALL Native_HasInteractionPermission(AMX*, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	if (!state || params[2] < 0 || params[2] > 63) return 0;
	if (state->permissions & PERMISSION_ADMINISTRATOR) return 1;
	return (state->permissions >> static_cast<unsigned>(params[2])) & 1ULL ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetInteractionData(AMX* amx, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	return state && hasParams(params, 3) ? writeString(amx, params[2], params[3], state->data.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)) : 0;
}

cell AMX_NATIVE_CALL Native_IsInteractionResponded(AMX*, cell* params)
{
	const InteractionState* state = interactionFor(params[1]);
	return state && state->response != ResponseState::None ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Interaction response natives
// ---------------------------------------------------------------------------

cell AMX_NATIVE_CALL Native_DeferInteraction(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	return state && hasParams(params, 2) && deferInteraction(*state, params[2] != 0) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_DeferInteractionUpdate(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	return state && deferInteractionUpdate(*state) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_RespondInteraction(AMX* amx, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 3)) return 0;
	MessageBuilder message;
	message.content = getAmxString(amx, params[2]);
	DiscordJson body;
	std::string error;
	if (!renderMessage(message, g_components, params[3] != 0, body, error))
	{
		warnNative("DBR_RespondInteraction", error);
		return 0;
	}
	return respondWithMessage(*state, std::move(body), params[3] != 0) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_RespondInteractionEmbed(AMX* amx, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	const auto embed = g_embeds.find(params[2]);
	if (!state || embed == g_embeds.end() || !hasParams(params, 4)) return 0;
	MessageBuilder message;
	message.content = getAmxString(amx, params[3]);
	message.embeds.push_back(std::move(embed->second));
	g_embeds.erase(embed);
	DiscordJson body;
	std::string error;
	if (!renderMessage(message, g_components, params[4] != 0, body, error))
	{
		warnNative("DBR_RespondInteractionEmbed", error);
		return 0;
	}
	return respondWithMessage(*state, std::move(body), params[4] != 0) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_RespondInteractionMessage(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 3)) return 0;
	DiscordJson body;
	if (!takeBuilderPayload(params[2], params[3] != 0, body, "DBR_RespondInteractionMessage")) return 0;
	return respondWithMessage(*state, std::move(body), params[3] != 0) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_UpdateInteractionMessage(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 2)) return 0;
	DiscordJson body;
	if (!takeBuilderPayload(params[2], false, body, "DBR_UpdateInteractionMessage")) return 0;
	if (state->type != ComponentInteraction && state->type != ModalSubmitInteraction) return 0;
	if (state->response == ResponseState::None)
	{
		if (!sendInitialResponse(*state, 7, std::move(body), "DBR_UpdateInteractionMessage")) return 0;
		state->response = ResponseState::Replied;
		return 1;
	}
	if (state->response != ResponseState::DeferredUpdate) return 0;
	return sendWebhookRequest(*state, http::verb::patch, "/messages/@original", body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), "DBR_UpdateInteractionMessage") ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_EditInteractionResponse(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 2)) return 0;
	DiscordJson body;
	if (!takeBuilderPayload(params[2], false, body, "DBR_EditInteractionResponse")) return 0;
	if (state->response == ResponseState::None || state->response == ResponseState::Modal ||
		state->response == ResponseState::Autocomplete) return 0;
	return sendWebhookRequest(*state, http::verb::patch, "/messages/@original", body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), "DBR_EditInteractionResponse") ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_DeleteInteractionResponse(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	if (!state || state->response == ResponseState::None || state->response == ResponseState::Modal ||
		state->response == ResponseState::Autocomplete) return 0;
	return sendWebhookRequest(*state, http::verb::delete_, "/messages/@original", std::string(), "DBR_DeleteInteractionResponse") ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SendInteractionFollowup(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	if (!state || !hasParams(params, 3)) return 0;
	DiscordJson body;
	if (!takeBuilderPayload(params[2], params[3] != 0, body, "DBR_SendInteractionFollowup")) return 0;
	if (state->response == ResponseState::None || state->response == ResponseState::Modal ||
		state->response == ResponseState::Autocomplete) return 0;
	return sendWebhookRequest(*state, http::verb::post, "", body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), "DBR_SendInteractionFollowup") ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_ShowModal(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	const auto modal = g_modals.find(params[2]);
	if (modal == g_modals.end()) return 0;
	DiscordJson body;
	std::string error;
	const bool rendered = renderModal(modal->second, g_components, body, error);
	destroyModal(params[2]);
	if (!rendered)
	{
		warnNative("DBR_ShowModal", error);
		return 0;
	}
	if (!state || state->response != ResponseState::None ||
		(state->type != CommandInteraction && state->type != ComponentInteraction)) return 0;
	if (!sendInitialResponse(*state, 9, std::move(body), "DBR_ShowModal")) return 0;
	state->response = ResponseState::Modal;
	return 1;
}

bool addAutocompleteChoice(cell handle, const std::string& name, DiscordJson value)
{
	InteractionState* state = interactionFor(handle);
	if (!state || state->type != AutocompleteInteraction || state->response != ResponseState::None) return false;
	if (name.empty() || name.size() > 100 || state->autocompleteChoices.size() >= 25) return false;
	if (value.is_string() && value.get<std::string>().size() > 100) return false;
	state->autocompleteChoices.push_back({ { "name", name }, { "value", std::move(value) } });
	return true;
}

cell AMX_NATIVE_CALL Native_AddAutocompleteChoice(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	return addAutocompleteChoice(params[1], getAmxString(amx, params[2]), getAmxString(amx, params[3])) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_AddAutocompleteChoiceInt(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	return addAutocompleteChoice(params[1], getAmxString(amx, params[2]), static_cast<std::int64_t>(params[3])) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_AddAutocompleteChoiceFloat(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	return addAutocompleteChoice(params[1], getAmxString(amx, params[2]), static_cast<double>(amx_ctof(params[3]))) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SendAutocomplete(AMX*, cell* params)
{
	InteractionState* state = interactionFor(params[1]);
	return state && sendAutocomplete(*state) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetInteractionAutoDefer(AMX*, cell* params)
{
	g_autoDefer = params[1] != 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_RegisterInteractionHandler(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const int type = static_cast<int>(params[1]);
	const std::string key = getAmxString(amx, params[2]);
	const std::string callback = getAmxString(amx, params[3]);
	const bool prefix = params[4] != 0;
	if (type < CommandInteraction || type > ModalSubmitInteraction || key.empty() || callback.empty() || callback.size() > 31) return 0;
	NativePawnScript* script = pawnScriptFor(amx);
	int publicIndex = -1;
	if (!script || script->FindPublic(callback.c_str(), &publicIndex) != AMX_ERR_NONE || publicIndex < 0)
	{
		warnNative("DBR_RegisterHandler", "public '" + callback + "' is not exported by the calling script");
		return 0;
	}
	for (auto& handler : g_handlers)
	{
		if (handler.type == type && handler.key == key && handler.prefix == prefix)
		{
			handler.callback = callback;
			handler.scriptId = script->GetID();
			return 1;
		}
	}
	if (g_handlers.size() >= MAX_HANDLERS) return 0;
	g_handlers.push_back({ type, key, callback, script->GetID(), prefix });
	return 1;
}

cell AMX_NATIVE_CALL Native_UnregisterInteractionHandler(AMX* amx, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	const int type = static_cast<int>(params[1]);
	const std::string key = getAmxString(amx, params[2]);
	const size_t before = g_handlers.size();
	g_handlers.erase(std::remove_if(g_handlers.begin(), g_handlers.end(), [type, &key](const InteractionHandler& handler)
	{
		return handler.type == type && handler.key == key;
	}), g_handlers.end());
	return g_handlers.size() != before ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Command natives
// ---------------------------------------------------------------------------

cell AMX_NATIVE_CALL Native_CreateCommand(AMX* amx, cell* params)
{
	if (!hasParams(params, 5)) return 0;
	const std::string name = getAmxString(amx, params[1]);
	const std::string description = getAmxString(amx, params[2]);
	const std::string callback = getAmxString(amx, params[4]);
	const int type = static_cast<int>(params[5]);
	std::string guildId;
	if (params[3] != 0)
	{
		guildId = guildIdForHandle(params[3]);
		if (guildId.empty()) return 0;
	}
	NativePawnScript* script = pawnScriptFor(amx);
	if (!callback.empty())
	{
		int publicIndex = -1;
		if (callback.size() > 31 || !script || script->FindPublic(callback.c_str(), &publicIndex) != AMX_ERR_NONE || publicIndex < 0)
		{
			warnNative("DBR_CreateCommand", "public '" + callback + "' is not exported by the calling script");
			return 0;
		}
	}
	// A script that disappeared without an unload notification may leave its
	// registration behind; the new registration replaces it.
	if (const Handle existing = g_commands.findCommand(name, type, guildId))
	{
		const Command* previous = g_commands.getCommand(existing);
		if (previous && !pawnScriptForId(previous->ownerScriptId)) g_commands.destroyCommand(existing);
	}
	std::string error;
	const Handle handle = g_commands.createCommand(type, name, description, guildId, error);
	if (!handle)
	{
		warnNative("DBR_CreateCommand", error + " ('" + name + "')");
		return 0;
	}
	Command* command = g_commands.getCommand(handle);
	command->callback = callback;
	command->ownerScriptId = script ? script->GetID() : -1;
	g_commandsEverCreated = true;
	markCommandsChanged();
	return handle;
}

cell AMX_NATIVE_CALL Native_DestroyCommand(AMX*, cell* params)
{
	if (!g_commands.destroyCommand(params[1])) return 0;
	markCommandsChanged();
	return 1;
}

cell AMX_NATIVE_CALL Native_AddCommandOption(AMX* amx, cell* params)
{
	if (!hasParams(params, 6)) return 0;
	std::string error;
	const Handle option = g_commands.addOption(params[1], params[6], static_cast<int>(params[2]),
		getAmxString(amx, params[3]), getAmxString(amx, params[4]), params[5] != 0, error);
	if (!option)
	{
		warnNative("DBR_AddCommandOption", error);
		return 0;
	}
	markCommandsChanged();
	return option;
}

cell addCommandChoice(cell option, const std::string& name, const DiscordJson& value)
{
	std::string error;
	if (!g_commands.addChoice(option, name, value, error))
	{
		warnNative("DBR_AddOptionChoice", error);
		return 0;
	}
	markCommandsChanged();
	return 1;
}

cell AMX_NATIVE_CALL Native_AddCommandChoice(AMX* amx, cell* params)
{
	return hasParams(params, 3) ? addCommandChoice(params[1], getAmxString(amx, params[2]), getAmxString(amx, params[3])) : 0;
}

cell AMX_NATIVE_CALL Native_AddCommandChoiceInt(AMX* amx, cell* params)
{
	return hasParams(params, 3) ? addCommandChoice(params[1], getAmxString(amx, params[2]), static_cast<std::int64_t>(params[3])) : 0;
}

cell AMX_NATIVE_CALL Native_AddCommandChoiceFloat(AMX* amx, cell* params)
{
	return hasParams(params, 3) ? addCommandChoice(params[1], getAmxString(amx, params[2]), static_cast<double>(amx_ctof(params[3]))) : 0;
}

cell AMX_NATIVE_CALL Native_SetCommandOptionAutocomplete(AMX*, cell* params)
{
	std::string error;
	if (!hasParams(params, 2) || !g_commands.setAutocomplete(params[1], params[2] != 0, error))
	{
		if (!error.empty()) warnNative("DBR_SetOptionAutocomplete", error);
		return 0;
	}
	markCommandsChanged();
	return 1;
}

cell AMX_NATIVE_CALL Native_SetCommandOptionRange(AMX*, cell* params)
{
	CommandOption* option = hasParams(params, 3) ? g_commands.getOption(params[1]) : nullptr;
	if (!option) return 0;
	if (option->type != IntegerOption && option->type != NumberOption)
	{
		warnNative("DBR_SetOptionRange", "only integer and number options have a range; use DBR_SetOptionLength for string options");
		return 0;
	}
	const double minValue = amx_ctof(params[2]);
	const double maxValue = amx_ctof(params[3]);
	if (minValue > maxValue) return 0;
	option->minValue = minValue;
	option->maxValue = maxValue;
	markCommandsChanged();
	return 1;
}

cell AMX_NATIVE_CALL Native_SetCommandOptionLength(AMX*, cell* params)
{
	CommandOption* option = hasParams(params, 3) ? g_commands.getOption(params[1]) : nullptr;
	if (!option) return 0;
	if (option->type != StringOption)
	{
		warnNative("DBR_SetOptionLength", "only string options have a length; use DBR_SetOptionRange for integer and number options");
		return 0;
	}
	if (params[2] < 0 || params[3] < 1 || params[2] > params[3] || params[3] > 6000) return 0;
	option->minLength = static_cast<int>(params[2]);
	option->maxLength = static_cast<int>(params[3]);
	markCommandsChanged();
	return 1;
}

cell AMX_NATIVE_CALL Native_AddCommandOptionChannelType(AMX*, cell* params)
{
	CommandOption* option = hasParams(params, 2) ? g_commands.getOption(params[1]) : nullptr;
	if (!option || option->type != ChannelOption || params[2] < 0) return 0;
	const int channelType = static_cast<int>(params[2]);
	if (std::find(option->channelTypes.begin(), option->channelTypes.end(), channelType) == option->channelTypes.end())
	{
		option->channelTypes.push_back(channelType);
		markCommandsChanged();
	}
	return 1;
}

cell AMX_NATIVE_CALL Native_SetCommandPermissions(AMX* amx, cell* params)
{
	Command* command = hasParams(params, 2) ? g_commands.getCommand(params[1]) : nullptr;
	if (!command) return 0;
	const std::string permissions = getAmxString(amx, params[2]);
	if (!permissions.empty() && !std::all_of(permissions.begin(), permissions.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) return 0;
	command->hasPermissions = !permissions.empty();
	command->defaultMemberPermissions = permissions;
	markCommandsChanged();
	return 1;
}

cell AMX_NATIVE_CALL Native_SetCommandNsfw(AMX*, cell* params)
{
	Command* command = hasParams(params, 2) ? g_commands.getCommand(params[1]) : nullptr;
	if (!command) return 0;
	command->nsfw = params[2] != 0;
	markCommandsChanged();
	return 1;
}

cell AMX_NATIVE_CALL Native_SetCommandContexts(AMX*, cell* params)
{
	Command* command = hasParams(params, 4) ? g_commands.getCommand(params[1]) : nullptr;
	if (!command || (!params[2] && !params[3] && !params[4])) return 0;
	command->contexts.clear();
	if (params[2]) command->contexts.push_back(0);
	if (params[3]) command->contexts.push_back(1);
	if (params[4]) command->contexts.push_back(2);
	markCommandsChanged();
	return 1;
}

cell AMX_NATIVE_CALL Native_DeployCommands(AMX*, cell*)
{
	return deployCommands() ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetCommandAutoDeploy(AMX*, cell* params)
{
	g_commandAutoDeploy = params[1] != 0;
	return 1;
}

// ---------------------------------------------------------------------------
// Component natives
// ---------------------------------------------------------------------------

Component* componentOf(cell handle, int type)
{
	Component* component = g_components.get(handle);
	return component && (type == 0 || component->type == type) ? component : nullptr;
}

cell AMX_NATIVE_CALL Native_CreateActionRow(AMX*, cell*)
{
	return g_components.create(ActionRow);
}

cell AMX_NATIVE_CALL Native_CreateButton(AMX* amx, cell* params)
{
	if (!hasParams(params, 5)) return 0;
	const int style = static_cast<int>(params[1]);
	const std::string label = getAmxString(amx, params[2]);
	const std::string target = getAmxString(amx, params[3]);
	const std::string emoji = getAmxString(amx, params[4]);
	if (style < 1 || style > 6 || label.size() > 80 || target.size() > (style == 5 ? 512u : 100u)) return 0;
	const Handle handle = g_components.create(Button);
	Component* button = g_components.get(handle);
	if (!button) return 0;
	button->data["style"] = style;
	if (!label.empty()) button->data["label"] = label;
	if (!target.empty()) button->data[style == 5 ? "url" : style == 6 ? "sku_id" : "custom_id"] = target;
	if (!emoji.empty()) button->data["emoji"] = parseEmoji(emoji);
	if (params[5]) button->data["disabled"] = true;
	return handle;
}

cell AMX_NATIVE_CALL Native_CreateSelectMenu(AMX* amx, cell* params)
{
	if (!hasParams(params, 6)) return 0;
	const int type = static_cast<int>(params[1]);
	const std::string customId = getAmxString(amx, params[2]);
	const std::string placeholder = getAmxString(amx, params[3]);
	if (!isSelectType(type) || customId.empty() || customId.size() > 100 || placeholder.size() > 150) return 0;
	if (params[4] < 0 || params[5] < 1 || params[4] > params[5] || params[5] > 25) return 0;
	const Handle handle = g_components.create(type);
	Component* menu = g_components.get(handle);
	if (!menu) return 0;
	menu->data["custom_id"] = customId;
	if (!placeholder.empty()) menu->data["placeholder"] = placeholder;
	menu->data["min_values"] = params[4];
	menu->data["max_values"] = params[5];
	if (params[6]) menu->data["disabled"] = true;
	return handle;
}

cell AMX_NATIVE_CALL Native_AddSelectMenuOption(AMX* amx, cell* params)
{
	if (!hasParams(params, 6)) return 0;
	std::string error;
	if (!g_components.addSelectOption(params[1], getAmxString(amx, params[2]), getAmxString(amx, params[3]),
		getAmxString(amx, params[4]), getAmxString(amx, params[5]), params[6] != 0, error))
	{
		warnNative("DBR_AddSelectMenuOption", error);
		return 0;
	}
	return 1;
}

cell AMX_NATIVE_CALL Native_AddSelectMenuDefault(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	static constexpr const char* kTypes[] = { "", "user", "role", "channel" };
	if (params[3] < 1 || params[3] > 3) return 0;
	std::string error;
	if (!g_components.addSelectDefaultValue(params[1], getAmxString(amx, params[2]), kTypes[params[3]], error))
	{
		warnNative("DBR_AddSelectMenuDefault", error);
		return 0;
	}
	return 1;
}

cell AMX_NATIVE_CALL Native_AddSelectMenuChannelType(AMX*, cell* params)
{
	Component* menu = hasParams(params, 2) ? componentOf(params[1], ChannelSelect) : nullptr;
	if (!menu || params[2] < 0) return 0;
	DiscordJson& types = menu->data["channel_types"];
	if (!types.is_array()) types = DiscordJson::array();
	types.push_back(params[2]);
	return 1;
}

cell AMX_NATIVE_CALL Native_SetSelectMenuRequired(AMX*, cell* params)
{
	Component* menu = hasParams(params, 2) ? g_components.get(params[1]) : nullptr;
	if (!menu || !isSelectType(menu->type)) return 0;
	menu->data["required"] = params[2] != 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_CreateTextInput(AMX* amx, cell* params)
{
	if (!hasParams(params, 7)) return 0;
	const std::string customId = getAmxString(amx, params[1]);
	const int style = static_cast<int>(params[2]);
	const std::string placeholder = getAmxString(amx, params[3]);
	const std::string value = getAmxString(amx, params[4]);
	if (customId.empty() || customId.size() > 100 || (style != 1 && style != 2) || placeholder.size() > 100 || value.size() > 4000) return 0;
	if (params[5] < 0 || params[6] < 1 || params[5] > params[6] || params[6] > 4000) return 0;
	const Handle handle = g_components.create(TextInput);
	Component* input = g_components.get(handle);
	if (!input) return 0;
	input->data["custom_id"] = customId;
	input->data["style"] = style;
	if (!placeholder.empty()) input->data["placeholder"] = placeholder;
	if (!value.empty()) input->data["value"] = value;
	if (params[5] > 0) input->data["min_length"] = params[5];
	input->data["max_length"] = params[6];
	input->data["required"] = params[7] != 0;
	return handle;
}

cell AMX_NATIVE_CALL Native_CreateFileUpload(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string customId = getAmxString(amx, params[1]);
	if (customId.empty() || customId.size() > 100 || params[2] < 0 || params[3] < 1 || params[2] > params[3] || params[3] > 10) return 0;
	const Handle handle = g_components.create(FileUpload);
	Component* upload = g_components.get(handle);
	if (!upload) return 0;
	upload->data["custom_id"] = customId;
	upload->data["min_values"] = params[2];
	upload->data["max_values"] = params[3];
	upload->data["required"] = params[4] != 0;
	return handle;
}

cell AMX_NATIVE_CALL Native_CreateTextDisplay(AMX* amx, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const std::string content = getAmxString(amx, params[1]);
	if (content.empty() || content.size() > 4000) return 0;
	const Handle handle = g_components.create(TextDisplay);
	if (Component* display = g_components.get(handle)) display->data["content"] = content;
	return handle;
}

cell AMX_NATIVE_CALL Native_CreateSeparator(AMX*, cell* params)
{
	if (!hasParams(params, 2) || (params[2] != 1 && params[2] != 2)) return 0;
	const Handle handle = g_components.create(Separator);
	if (Component* separator = g_components.get(handle))
	{
		separator->data["divider"] = params[1] != 0;
		separator->data["spacing"] = params[2];
	}
	return handle;
}

cell AMX_NATIVE_CALL Native_CreateContainer(AMX*, cell* params)
{
	if (!hasParams(params, 2) || params[1] > 0xFFFFFF) return 0;
	const Handle handle = g_components.create(Container);
	if (Component* container = g_components.get(handle))
	{
		if (params[1] >= 0) container->data["accent_color"] = params[1];
		if (params[2]) container->data["spoiler"] = true;
	}
	return handle;
}

cell AMX_NATIVE_CALL Native_CreateSection(AMX*, cell*)
{
	return g_components.create(Section);
}

cell AMX_NATIVE_CALL Native_CreateThumbnail(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const std::string url = getAmxString(amx, params[1]);
	const std::string description = getAmxString(amx, params[2]);
	if (url.empty() || description.size() > 1024) return 0;
	const Handle handle = g_components.create(Thumbnail);
	if (Component* thumbnail = g_components.get(handle))
	{
		thumbnail->data["media"] = { { "url", url } };
		if (!description.empty()) thumbnail->data["description"] = description;
		if (params[3]) thumbnail->data["spoiler"] = true;
	}
	return handle;
}

cell AMX_NATIVE_CALL Native_CreateMediaGallery(AMX*, cell*)
{
	return g_components.create(MediaGallery);
}

cell AMX_NATIVE_CALL Native_AddMediaGalleryItem(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	std::string error;
	if (!g_components.addMediaItem(params[1], getAmxString(amx, params[2]), getAmxString(amx, params[3]), params[4] != 0, error))
	{
		warnNative("DBR_AddMediaGalleryItem", error);
		return 0;
	}
	return 1;
}

cell AMX_NATIVE_CALL Native_CreateLabel(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const std::string label = getAmxString(amx, params[1]);
	const std::string description = getAmxString(amx, params[3]);
	if (label.empty() || label.size() > 45 || description.size() > 100) return 0;
	const Handle handle = g_components.create(Label);
	Component* wrapper = g_components.get(handle);
	if (!wrapper) return 0;
	wrapper->data["label"] = label;
	if (!description.empty()) wrapper->data["description"] = description;
	std::string error;
	if (!g_components.addChild(handle, params[2], error))
	{
		g_components.destroy(handle);
		warnNative("DBR_CreateLabel", error);
		return 0;
	}
	return handle;
}

cell AMX_NATIVE_CALL Native_AddComponent(AMX*, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	std::string error;
	if (!g_components.addChild(params[1], params[2], error))
	{
		warnNative("DBR_AddComponent", error);
		return 0;
	}
	return 1;
}

cell AMX_NATIVE_CALL Native_SetSectionAccessory(AMX*, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	std::string error;
	if (!g_components.setAccessory(params[1], params[2], error))
	{
		warnNative("DBR_SetSectionAccessory", error);
		return 0;
	}
	return 1;
}

cell AMX_NATIVE_CALL Native_SetComponentId(AMX*, cell* params)
{
	Component* component = hasParams(params, 2) ? g_components.get(params[1]) : nullptr;
	if (!component || params[2] < 0) return 0;
	if (params[2] == 0) component->data.erase("id");
	else component->data["id"] = params[2];
	return 1;
}

cell AMX_NATIVE_CALL Native_SetComponentDisabled(AMX*, cell* params)
{
	Component* component = hasParams(params, 2) ? g_components.get(params[1]) : nullptr;
	if (!component || (component->type != Button && !isSelectType(component->type))) return 0;
	component->data["disabled"] = params[2] != 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_DestroyComponent(AMX*, cell* params)
{
	const Component* component = g_components.get(params[1]);
	// Attached components are owned by their parent, builder or modal.
	if (!component || component->attached) return 0;
	return g_components.destroy(params[1]) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Message builder natives
// ---------------------------------------------------------------------------

MessageBuilder* builderFor(cell handle)
{
	const auto it = g_builders.find(handle);
	return it == g_builders.end() ? nullptr : &it->second;
}

cell AMX_NATIVE_CALL Native_CreateMessageBuilder(AMX* amx, cell* params)
{
	if (g_builders.size() >= MAX_BUILDERS) return 0;
	MessageBuilder builder;
	if (hasParams(params, 1)) builder.content = getAmxString(amx, params[1]);
	const cell handle = g_nextBuilder++;
	g_builders.emplace(handle, std::move(builder));
	return handle;
}

cell AMX_NATIVE_CALL Native_DestroyMessageBuilder(AMX*, cell* params)
{
	if (!builderFor(params[1])) return 0;
	destroyBuilder(params[1]);
	return 1;
}

cell AMX_NATIVE_CALL Native_SetMessageBuilderContent(AMX* amx, cell* params)
{
	MessageBuilder* builder = hasParams(params, 2) ? builderFor(params[1]) : nullptr;
	if (!builder) return 0;
	builder->content = getAmxString(amx, params[2]);
	return 1;
}

cell AMX_NATIVE_CALL Native_AddMessageBuilderEmbed(AMX*, cell* params)
{
	MessageBuilder* builder = hasParams(params, 2) ? builderFor(params[1]) : nullptr;
	const auto embed = g_embeds.find(params[2]);
	if (!builder || embed == g_embeds.end() || builder->embeds.size() >= 10) return 0;
	builder->embeds.push_back(std::move(embed->second));
	g_embeds.erase(embed);
	return 1;
}

cell AMX_NATIVE_CALL Native_AddMessageBuilderComponent(AMX*, cell* params)
{
	MessageBuilder* builder = hasParams(params, 2) ? builderFor(params[1]) : nullptr;
	if (!builder || builder->components.size() >= 40 || !g_components.attach(params[2])) return 0;
	builder->components.push_back(params[2]);
	return 1;
}

cell AMX_NATIVE_CALL Native_SetMessageBuilderTts(AMX*, cell* params)
{
	MessageBuilder* builder = hasParams(params, 2) ? builderFor(params[1]) : nullptr;
	if (!builder) return 0;
	builder->tts = params[2] != 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_SetMessageBuilderSilent(AMX*, cell* params)
{
	MessageBuilder* builder = hasParams(params, 2) ? builderFor(params[1]) : nullptr;
	if (!builder) return 0;
	builder->silent = params[2] != 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_SetMessageBuilderSuppressEmbeds(AMX*, cell* params)
{
	MessageBuilder* builder = hasParams(params, 2) ? builderFor(params[1]) : nullptr;
	if (!builder) return 0;
	builder->suppressEmbeds = params[2] != 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_SetMessageBuilderReply(AMX*, cell* params)
{
	MessageBuilder* builder = hasParams(params, 3) ? builderFor(params[1]) : nullptr;
	std::string channelId;
	std::string messageId;
	if (!builder || !messageRefForHandle(params[2], channelId, messageId)) return 0;
	builder->replyMessageId = messageId;
	builder->replyMention = params[3] != 0;
	return 1;
}

cell AMX_NATIVE_CALL Native_SetMessageBuilderMentions(AMX*, cell* params)
{
	MessageBuilder* builder = hasParams(params, 4) ? builderFor(params[1]) : nullptr;
	if (!builder) return 0;
	DiscordJson parse = DiscordJson::array();
	if (params[2]) parse.push_back("users");
	if (params[3]) parse.push_back("roles");
	if (params[4]) parse.push_back("everyone");
	builder->allowedMentions = DiscordJson { { "parse", std::move(parse) } };
	return 1;
}

cell AMX_NATIVE_CALL Native_SendMessageBuilder(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string channelId = channelIdForHandle(params[1]);
	std::shared_ptr<PreparedPawnCallback> callback;
	if (channelId.empty() || !capturePawnCallback(amx, params[3], params[4], params, 5, callback))
	{
		destroyBuilder(params[2]);
		return 0;
	}
	DiscordJson body;
	if (!takeBuilderPayload(params[2], false, body, "DBR_SendMessage")) return 0;
	return submitAction("DBR_SendMessage", [channelId, payload = body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)](DiscordHTTP& rest)
	{
		return rest.sendMessagePayload(channelId, payload);
	}, [callback](const DiscordHTTP::Response& response)
	{
		completeMessageResponse(response.success, response.body, callback);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SendDirectMessage(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string userId = userIdForHandle(params[1]);
	std::shared_ptr<PreparedPawnCallback> callback;
	if (userId.empty() || !capturePawnCallback(amx, params[3], params[4], params, 5, callback))
	{
		destroyBuilder(params[2]);
		return 0;
	}
	DiscordJson body;
	if (!takeBuilderPayload(params[2], false, body, "DBR_SendDirectMessage")) return 0;
	return submitAction("DBR_SendDirectMessage", [userId, channelId = std::string(), payload = body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)](DiscordHTTP& rest) mutable
	{
		// Preserve the completed first step if sending is deferred by a bucket.
		if (channelId.empty())
		{
			const DiscordHTTP::Response channel = rest.createDM(userId);
			if (!channel.success) return channel;
			const DiscordJson data = DiscordJson::parse(channel.body, nullptr, false);
			channelId = data.is_object() ? jsonString(data, "id") : std::string();
			if (channelId.empty()) return DiscordHTTP::Response { 0, "Discord did not return a DM channel", false, {}, 0.0, false };
		}
		return rest.sendMessagePayload(channelId, payload);
	}, [callback](const DiscordHTTP::Response& response)
	{
		completeMessageResponse(response.success, response.body, callback);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_EditMessageWithBuilder(AMX*, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	std::string channelId;
	std::string messageId;
	if (!messageRefForHandle(params[1], channelId, messageId))
	{
		destroyBuilder(params[2]);
		return 0;
	}
	DiscordJson body;
	if (!takeBuilderPayload(params[2], false, body, "DBR_EditMessageWithBuilder")) return 0;
	return submitAction("DBR_EditMessageWithBuilder", [channelId, messageId, payload = body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)](DiscordHTTP& rest)
	{
		return rest.editMessagePayload(channelId, messageId, payload);
	}) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Modal natives
// ---------------------------------------------------------------------------

Modal* modalFor(cell handle)
{
	const auto it = g_modals.find(handle);
	return it == g_modals.end() ? nullptr : &it->second;
}

cell AMX_NATIVE_CALL Native_CreateModal(AMX* amx, cell* params)
{
	if (!hasParams(params, 2) || g_modals.size() >= MAX_BUILDERS) return 0;
	Modal modal;
	modal.customId = getAmxString(amx, params[1]);
	modal.title = getAmxString(amx, params[2]);
	if (modal.customId.empty() || modal.customId.size() > 100 || modal.title.empty() || modal.title.size() > 45) return 0;
	const cell handle = g_nextModal++;
	g_modals.emplace(handle, std::move(modal));
	return handle;
}

cell AMX_NATIVE_CALL Native_DestroyModal(AMX*, cell* params)
{
	if (!modalFor(params[1])) return 0;
	destroyModal(params[1]);
	return 1;
}

cell AMX_NATIVE_CALL Native_AddModalComponent(AMX*, cell* params)
{
	Modal* modal = hasParams(params, 2) ? modalFor(params[1]) : nullptr;
	if (!modal || modal->components.size() >= 5 || !g_components.attach(params[2])) return 0;
	modal->components.push_back(params[2]);
	return 1;
}

cell AMX_NATIVE_CALL Native_AddModalTextInput(AMX* amx, cell* params)
{
	Modal* modal = hasParams(params, 10) ? modalFor(params[1]) : nullptr;
	if (!modal || modal->components.size() >= 5) return 0;
	const std::string customId = getAmxString(amx, params[2]);
	const std::string label = getAmxString(amx, params[3]);
	const int style = static_cast<int>(params[4]);
	const std::string placeholder = getAmxString(amx, params[5]);
	const std::string value = getAmxString(amx, params[6]);
	const std::string description = getAmxString(amx, params[10]);
	if (customId.empty() || customId.size() > 100 || label.empty() || label.size() > 45 || description.size() > 100 ||
		(style != 1 && style != 2) || placeholder.size() > 100 || value.size() > 4000 ||
		params[7] < 0 || params[8] < 1 || params[7] > params[8] || params[8] > 4000) return 0;

	const Handle input = g_components.create(TextInput);
	const Handle wrapper = g_components.create(Label);
	Component* inputComponent = g_components.get(input);
	Component* labelComponent = g_components.get(wrapper);
	std::string error;
	if (!inputComponent || !labelComponent)
	{
		g_components.destroy(input);
		g_components.destroy(wrapper);
		return 0;
	}
	inputComponent->data["custom_id"] = customId;
	inputComponent->data["style"] = style;
	if (!placeholder.empty()) inputComponent->data["placeholder"] = placeholder;
	if (!value.empty()) inputComponent->data["value"] = value;
	if (params[7] > 0) inputComponent->data["min_length"] = params[7];
	inputComponent->data["max_length"] = params[8];
	inputComponent->data["required"] = params[9] != 0;
	labelComponent->data["label"] = label;
	if (!description.empty()) labelComponent->data["description"] = description;
	if (!g_components.addChild(wrapper, input, error) || !g_components.attach(wrapper))
	{
		g_components.destroy(wrapper);
		g_components.destroy(input);
		return 0;
	}
	modal->components.push_back(wrapper);
	return 1;
}

// ---------------------------------------------------------------------------
// Embed extras
// ---------------------------------------------------------------------------

cell AMX_NATIVE_CALL Native_SetEmbedAuthor(AMX* amx, cell* params)
{
	const auto embed = g_embeds.find(params[1]);
	if (embed == g_embeds.end() || !hasParams(params, 4)) return 0;
	const std::string name = getAmxString(amx, params[2]);
	if (name.size() > 256) return 0;
	embed->second.authorName = name;
	embed->second.authorUrl = getAmxString(amx, params[3]);
	embed->second.authorIconUrl = getAmxString(amx, params[4]);
	return 1;
}

cell AMX_NATIVE_CALL Native_ClearEmbedFields(AMX*, cell* params)
{
	const auto embed = g_embeds.find(params[1]);
	if (embed == g_embeds.end()) return 0;
	embed->second.fields.clear();
	return 1;
}
}

void appendInteractionNatives(std::vector<AMX_NATIVE_INFO>& natives)
{
	static const AMX_NATIVE_INFO kNatives[] = {
		{ "DBR_GetInteractionType", Native_GetInteractionType },
		{ "DBR_GetInteractionID", Native_GetInteractionId },
		{ "DBR_GetInteractionUser", Native_GetInteractionUser },
		{ "DBR_GetInteractionChannel", Native_GetInteractionChannel },
		{ "DBR_GetInteractionGuild", Native_GetInteractionGuild },
		{ "DBR_GetInteractionMessage", Native_GetInteractionMessage },
		{ "DBR_GetInteractionLocale", Native_GetInteractionLocale },
		{ "DBR_GetInteractionCommandName", Native_GetInteractionCommandName },
		{ "DBR_GetInteractionCommandType", Native_GetInteractionCommandType },
		{ "DBR_GetInteractionSubcommand", Native_GetInteractionSubcommand },
		{ "DBR_GetInteractionSubGroup", Native_GetInteractionSubcommandGroup },
		{ "DBR_GetInteractionTargetID", Native_GetInteractionTargetId },
		{ "DBR_GetInteractionCustomID", Native_GetInteractionCustomId },
		{ "DBR_GetInteractionComponentType", Native_GetInteractionComponentType },
		{ "DBR_GetInteractionValueCount", Native_GetInteractionValueCount },
		{ "DBR_GetInteractionValue", Native_GetInteractionValue },
		{ "DBR_HasInteractionOption", Native_HasInteractionOption },
		{ "DBR_GetInteractionOptionType", Native_GetInteractionOptionType },
		{ "DBR_GetInteractionOptionString", Native_GetInteractionOptionString },
		{ "DBR_GetInteractionMentionCount", Native_GetInteractionMentionCount },
		{ "DBR_GetInteractionMention", Native_GetInteractionMention },
		{ "DBR_GetInteractionOptionInt", Native_GetInteractionOptionInt },
		{ "DBR_GetInteractionOptionFloat", Native_GetInteractionOptionFloat },
		{ "DBR_GetInteractionOptionBool", Native_GetInteractionOptionBool },
		{ "DBR_GetInteractionOptionUser", Native_GetInteractionOptionUser },
		{ "DBR_GetInteractionOptionChannel", Native_GetInteractionOptionChannel },
		{ "DBR_GetInteractionOptionRole", Native_GetInteractionOptionRole },
		{ "DBR_GetInteractionFocusedOption", Native_GetInteractionFocusedOption },
		{ "DBR_GetInteractionAttachmentURL", Native_GetInteractionAttachmentUrl },
		{ "DBR_HasInteractionPermission", Native_HasInteractionPermission },
		{ "DBR_GetInteractionData", Native_GetInteractionData },
		{ "DBR_IsInteractionResponded", Native_IsInteractionResponded },

		{ "DBR_DeferInteraction", Native_DeferInteraction },
		{ "DBR_DeferInteractionUpdate", Native_DeferInteractionUpdate },
		{ "DBR_RespondInteraction", Native_RespondInteraction },
		{ "DBR_RespondInteractionEmbed", Native_RespondInteractionEmbed },
		{ "DBR_RespondInteractionMessage", Native_RespondInteractionMessage },
		{ "DBR_UpdateInteractionMessage", Native_UpdateInteractionMessage },
		{ "DBR_EditInteractionResponse", Native_EditInteractionResponse },
		{ "DBR_DeleteInteractionResponse", Native_DeleteInteractionResponse },
		{ "DBR_SendInteractionFollowup", Native_SendInteractionFollowup },
		{ "DBR_ShowModal", Native_ShowModal },
		{ "DBR_AddAutocompleteChoice", Native_AddAutocompleteChoice },
		{ "DBR_AddAutocompleteChoiceInt", Native_AddAutocompleteChoiceInt },
		{ "DBR_AddAutocompleteChoiceFloat", Native_AddAutocompleteChoiceFloat },
		{ "DBR_SendAutocomplete", Native_SendAutocomplete },
		{ "DBR_SetInteractionAutoDefer", Native_SetInteractionAutoDefer },
		{ "DBR_RegisterHandler", Native_RegisterInteractionHandler },
		{ "DBR_UnregisterHandler", Native_UnregisterInteractionHandler },

		{ "DBR_CreateCommand", Native_CreateCommand },
		{ "DBR_DestroyCommand", Native_DestroyCommand },
		{ "DBR_AddCommandOption", Native_AddCommandOption },
		{ "DBR_AddOptionChoice", Native_AddCommandChoice },
		{ "DBR_AddOptionChoiceInt", Native_AddCommandChoiceInt },
		{ "DBR_AddOptionChoiceFloat", Native_AddCommandChoiceFloat },
		{ "DBR_SetOptionAutocomplete", Native_SetCommandOptionAutocomplete },
		{ "DBR_SetOptionRange", Native_SetCommandOptionRange },
		{ "DBR_SetOptionLength", Native_SetCommandOptionLength },
		{ "DBR_AddOptionChannelType", Native_AddCommandOptionChannelType },
		{ "DBR_SetCommandPermissions", Native_SetCommandPermissions },
		{ "DBR_SetCommandNSFW", Native_SetCommandNsfw },
		{ "DBR_SetCommandContexts", Native_SetCommandContexts },
		{ "DBR_DeployCommands", Native_DeployCommands },
		{ "DBR_SetCommandAutoDeploy", Native_SetCommandAutoDeploy },

		{ "DBR_CreateActionRow", Native_CreateActionRow },
		{ "DBR_CreateButton", Native_CreateButton },
		{ "DBR_CreateSelectMenu", Native_CreateSelectMenu },
		{ "DBR_AddSelectMenuOption", Native_AddSelectMenuOption },
		{ "DBR_AddSelectMenuDefault", Native_AddSelectMenuDefault },
		{ "DBR_AddSelectMenuChannelType", Native_AddSelectMenuChannelType },
		{ "DBR_SetSelectMenuRequired", Native_SetSelectMenuRequired },
		{ "DBR_CreateTextInput", Native_CreateTextInput },
		{ "DBR_CreateFileUpload", Native_CreateFileUpload },
		{ "DBR_CreateTextDisplay", Native_CreateTextDisplay },
		{ "DBR_CreateSeparator", Native_CreateSeparator },
		{ "DBR_CreateContainer", Native_CreateContainer },
		{ "DBR_CreateSection", Native_CreateSection },
		{ "DBR_CreateThumbnail", Native_CreateThumbnail },
		{ "DBR_CreateMediaGallery", Native_CreateMediaGallery },
		{ "DBR_AddMediaGalleryItem", Native_AddMediaGalleryItem },
		{ "DBR_CreateLabel", Native_CreateLabel },
		{ "DBR_AddComponent", Native_AddComponent },
		{ "DBR_SetSectionAccessory", Native_SetSectionAccessory },
		{ "DBR_SetComponentID", Native_SetComponentId },
		{ "DBR_SetComponentDisabled", Native_SetComponentDisabled },
		{ "DBR_DestroyComponent", Native_DestroyComponent },

		{ "DBR_CreateMessageBuilder", Native_CreateMessageBuilder },
		{ "DBR_DestroyMessageBuilder", Native_DestroyMessageBuilder },
		{ "DBR_SetBuilderContent", Native_SetMessageBuilderContent },
		{ "DBR_AddBuilderEmbed", Native_AddMessageBuilderEmbed },
		{ "DBR_AddBuilderComponent", Native_AddMessageBuilderComponent },
		{ "DBR_SetBuilderTTS", Native_SetMessageBuilderTts },
		{ "DBR_SetBuilderSilent", Native_SetMessageBuilderSilent },
		{ "DBR_SetBuilderSuppressEmbeds", Native_SetMessageBuilderSuppressEmbeds },
		{ "DBR_SetBuilderReply", Native_SetMessageBuilderReply },
		{ "DBR_SetBuilderMentions", Native_SetMessageBuilderMentions },
		{ "DBR_SendMessage", Native_SendMessageBuilder },
		{ "DBR_SendDirectMessage", Native_SendDirectMessage },
		{ "DBR_EditMessageWithBuilder", Native_EditMessageWithBuilder },

		{ "DBR_CreateModal", Native_CreateModal },
		{ "DBR_DestroyModal", Native_DestroyModal },
		{ "DBR_AddModalComponent", Native_AddModalComponent },
		{ "DBR_AddModalTextInput", Native_AddModalTextInput },

		{ "DBR_SetEmbedAuthor", Native_SetEmbedAuthor },
		{ "DBR_ClearEmbedFields", Native_ClearEmbedFields },
	};
	natives.insert(natives.end(), std::begin(kNatives), std::end(kNatives));
}

void resetInteractionState()
{
	g_interactions.clear();
	g_nextInteraction = 1;
	g_components.clear();
	g_builders.clear();
	g_nextBuilder = 1;
	g_modals.clear();
	g_nextModal = 1;
	g_commands.clear();
	g_handlers.clear();
	g_commandsDirty = false;
	g_commandsEverCreated = false;
	g_commandWaitWarned = false;
	g_deployedScopes.clear();
	g_pendingScopes.clear();
}

void forgetInteractionScript(int scriptId)
{
	g_handlers.erase(std::remove_if(g_handlers.begin(), g_handlers.end(), [scriptId](const InteractionHandler& handler)
	{
		return handler.scriptId == scriptId;
	}), g_handlers.end());

	std::vector<Handle> owned;
	for (const auto& entry : g_commands.commands())
	{
		if (entry.second.ownerScriptId == scriptId) owned.push_back(entry.first);
	}
	for (const Handle command : owned) g_commands.destroyCommand(command);
	if (!owned.empty()) markCommandsChanged();
}

void serviceInteractionState()
{
	const auto now = Clock::now();
	for (auto it = g_interactions.begin(); it != g_interactions.end();)
	{
		if (now - it->second.receivedAt < INTERACTION_LIFETIME) break;
		forgetInteractionMessage(it->second);
		it = g_interactions.erase(it);
	}

	if (g_commandsDirty && g_commandAutoDeploy && g_commandsEverCreated && now - g_commandsChangedAt >= COMMAND_DEPLOY_DELAY)
	{
		DiscordBot* bot = nativeBot();
		if (bot && bot->isConnected())
		{
			g_commandWaitWarned = false;
			deployCommands();
		}
		else if (!g_commandWaitWarned && now - g_commandsChangedAt >= COMMAND_WAIT_WARNING_DELAY)
		{
			g_commandWaitWarned = true;
			logWarning(bot
				? "[DiscordBridge] application commands are waiting for the bot to become ready; check the token and "
				  "that every intent passed to DBR_ConnectBot is enabled in the Discord Developer Portal"
				: "[DiscordBridge] application commands are waiting for DBR_ConnectBot or a configured discord_bot_token");
		}
	}
}

void onInteractionBotDisconnected()
{
	// Interaction tokens and the record of published commands belong to the
	// session that ended.  The next bot may even use another application, so
	// publish every command again once it is ready.
	g_interactions.clear();
	g_deployedScopes.clear();
	g_pendingScopes.clear();
	g_commandWaitWarned = false;
	if (g_commandsEverCreated && !g_commands.commands().empty())
	{
		g_commandsDirty = true;
		g_commandsChangedAt = Clock::now();
	}
}

void onInteractionBotReady()
{
	// Commands still waiting are published right away.  Already published
	// commands stay as they are: forcing a new publish here duplicated the one
	// that runs as soon as the gateway connects.
	if (!g_commandsEverCreated || !g_commandsDirty) return;
	g_commandsChangedAt = Clock::now() - COMMAND_DEPLOY_DELAY;
}
}

void HandleDiscordInteractionPayload(const std::string& json)
{
	DiscordNatives::dispatchInteraction(json);
}
