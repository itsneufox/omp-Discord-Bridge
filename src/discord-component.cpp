/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#include "discord-component.hpp"
#include "discord-log.hpp"
#include "natives.hpp"
#include "discord-http.hpp"
#include "discord-json.hpp"
#include "discord-channel.hpp"
#include "discord-guild.hpp"
#include "discord-user.hpp"
#include "discord-message.hpp"
#include "discord-role.hpp"
#include "utils.hpp"
#include "version.hpp"
#include <algorithm>
#include <climits>
#include <cstdlib>

DiscordBridgeComponent* DiscordBridgeComponent::instance_ = nullptr;

namespace
{
template <typename... Args>
void CallPawnPublic(IPawnComponent* pawn, const char* name, Args... args)
{
	if (!pawn)
	{
		return;
	}

	auto callIfPresent = [name, &args...](IPawnScript* script) -> bool
	{
		if (!script)
		{
			return false;
		}

		int publicIndex = -1;
		if (script->FindPublic(name, &publicIndex) != AMX_ERR_NONE || publicIndex < 0 || publicIndex == INT_MAX)
		{
			return false;
		}

		cell result = DefaultReturnValue_True;
		const int error = script->CallChecked(publicIndex, result, args...);
		if (error != AMX_ERR_NONE)
		{
			script->PrintError(error);
		}
		return true;
	};

	if (callIfPresent(pawn->mainScript()))
	{
		return;
	}

	for (IPawnScript* script : pawn->sideScripts())
	{
		if (callIfPresent(script))
		{
			return;
		}
	}
}
}

StringView DiscordBridgeComponent::componentName() const
{
	return "discord-bridge";
}

SemanticVersion DiscordBridgeComponent::componentVersion() const
{
	return SemanticVersion(DISCORD_BRIDGE_VERSION_MAJOR, DISCORD_BRIDGE_VERSION_MINOR, DISCORD_BRIDGE_VERSION_PATCH);
}

void DiscordBridgeComponent::onLoad(ICore* c)
{
	sampMode_ = false;
	core_ = c;
	DiscordLogSetMainThread();
	if (!core_)
	{
		return;
	}

	core_->getEventDispatcher().addEventHandler(this);
	core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
}

void DiscordBridgeComponent::onInit(IComponentList* components)
{
	if (!components)
	{
		return;
	}

	pawn_ = components->queryComponent<IPawnComponent>();
	if (pawn_)
	{
		pawn_->getEventDispatcher().addEventHandler(this);
	}
}

void DiscordBridgeComponent::onReady()
{
	isInitialized_ = true;
	loadConfiguration();
	startUpdateCheck();
	connectConfiguredBot();
}

void DiscordBridgeComponent::startUpdateCheck()
{
	if (!configuredUpdateCheck_ || updateChecker_) return;
	try
	{
		updateChecker_ = std::make_unique<DiscordUpdateChecker>();
		updateChecker_->start(DISCORD_BRIDGE_VERSION);
	}
	catch (...)
	{
		// A best-effort update check must never prevent the component from loading.
		updateChecker_.reset();
	}
}

void DiscordBridgeComponent::loadConfiguration()
{
	// Scripts may call natives before this component's onReady (the gamemode
	// can load first), so the configuration is also read on first use.
	if (configurationLoaded_ || !core_) return;
	configurationLoaded_ = true;

	const char* envToken = std::getenv("DISCORD_BOT_TOKEN");
	const char* envIntents = std::getenv("DISCORD_BOT_INTENTS");
	const char* envChannelId = std::getenv("DISCORD_CHANNEL_ID");
	const char* envChannelName = std::getenv("DISCORD_CHANNEL_NAME");
	const char* envUpdateCheck = std::getenv("DISCORD_CHECK_FOR_UPDATES");

	configuredToken_.clear();
	configuredIntents_ = DISCORD_DEFAULT_INTENTS;
	configuredChannelId_.clear();
	configuredChannelName_.clear();

	if (envToken && *envToken)
	{
		configuredToken_ = envToken;
	}
	else if (core_)
	{
		const StringView cfgToken = core_->getConfig().getString("discord_bot_token");
		if (!cfgToken.empty())
		{
			configuredToken_.assign(cfgToken.data(), cfgToken.length());
		}
		else
		{
			const StringView dottedToken = core_->getConfig().getString("discord.bot_token");
			if (!dottedToken.empty()) configuredToken_.assign(dottedToken.data(), dottedToken.length());
		}
	}

	if (envIntents && *envIntents)
	{
		configuredIntents_ = std::atoi(envIntents);
	}
	else if (core_)
	{
		if (int* cfgIntents = core_->getConfig().getInt("discord_bot_intents"))
		{
			configuredIntents_ = *cfgIntents;
		}
		else if (int* dottedIntents = core_->getConfig().getInt("discord.intents"))
		{
			configuredIntents_ = *dottedIntents;
		}
	}

	if (const char* interval = std::getenv("DISCORD_BATCH_INTERVAL_MS"); interval && *interval)
	{
		configuredBatchIntervalMs_ = DiscordMessageBatchConfig::interval(interval);
	}
	else
	{
		int* configInterval = core_->getConfig().getInt("discord_batch_interval_ms");
		if (!configInterval) configInterval = core_->getConfig().getInt("discord.batch_interval_ms");
		configuredBatchIntervalMs_ = configInterval && *configInterval > 0 ? *configInterval : DiscordMessageBatchConfig::DEFAULT_INTERVAL_MS;
	}
	if (const char* enabled = std::getenv("DISCORD_BATCH_RATE_LIMITED"); enabled && *enabled)
	{
		configuredBatchRateLimited_ = DiscordMessageBatchConfig::enabled(enabled);
	}
	else
	{
		bool* configEnabled = core_->getConfig().getBool("discord_batch_rate_limited");
		if (!configEnabled) configEnabled = core_->getConfig().getBool("discord.batch_rate_limited");
		configuredBatchRateLimited_ = configEnabled && *configEnabled;
	}
	if (envUpdateCheck && *envUpdateCheck)
	{
		configuredUpdateCheck_ = DiscordMessageBatchConfig::enabled(envUpdateCheck);
	}
	else
	{
		bool* configEnabled = core_->getConfig().getBool("discord_check_for_updates");
		if (!configEnabled) configEnabled = core_->getConfig().getBool("discord.check_for_updates");
		configuredUpdateCheck_ = configEnabled ? *configEnabled : true;
	}

	if (envChannelId && *envChannelId)
	{
		configuredChannelId_ = envChannelId;
	}
	else if (core_)
	{
		const StringView cfgChannelId = core_->getConfig().getString("discord.channel_id");
		if (!cfgChannelId.empty())
		{
			configuredChannelId_.assign(cfgChannelId.data(), cfgChannelId.length());
		}
		else
		{
			const StringView flatChannelId = core_->getConfig().getString("discord_channel_id");
			if (!flatChannelId.empty())
			{
				configuredChannelId_.assign(flatChannelId.data(), flatChannelId.length());
			}
		}
	}

	if (envChannelName && *envChannelName)
	{
		configuredChannelName_ = envChannelName;
	}
	else if (core_)
	{
		const StringView cfgChannelName = core_->getConfig().getString("discord.channel_name");
		if (!cfgChannelName.empty())
		{
			configuredChannelName_.assign(cfgChannelName.data(), cfgChannelName.length());
		}
		else
		{
			const StringView flatChannelName = core_->getConfig().getString("discord_channel_name");
			if (!flatChannelName.empty())
			{
				configuredChannelName_.assign(flatChannelName.data(), flatChannelName.length());
			}
		}
	}

}

bool DiscordBridgeComponent::requestDisconnect()
{
	if (!bot_ || disconnectRequested_) return false;
	disconnectRequested_ = true;
	reconnectRequested_ = false;
	// Outside the bot's own dispatch (OnGameModeExit, commands, timers...) the
	// bot shuts down immediately, even when no further server tick will run.
	if (!insideBotUpdate_) performPendingDisconnect();
	return true;
}

void DiscordBridgeComponent::queueReconnect(StringView token, int intents)
{
	reconnectRequested_ = true;
	reconnectToken_.assign(token.data(), token.length());
	reconnectIntents_ = intents;
}

void DiscordBridgeComponent::performPendingDisconnect()
{
	if (!disconnectRequested_) return;
	disconnectRequested_ = false;

	const bool wasConnected = bot_ && bot_->isConnected();
	if (bot_)
	{
		bot_->disconnect();
		bot_.reset();
	}
	// Cached entities belong to the old session; handles, commands and
	// builders created by scripts stay valid.
	channels_.clear();
	guilds_.clear();
	users_.clear();
	messages_.clear();
	roles_.clear();
	NotifyDiscordNativesDisconnected();
	if (wasConnected) onBotDisconnectedEvent();

	if (reconnectRequested_)
	{
		reconnectRequested_ = false;
		if (hasConfiguredToken()) connectConfiguredBot();
		else if (!reconnectToken_.empty()) connectBot(reconnectToken_, reconnectIntents_);
		reconnectToken_.clear();
	}
}

bool DiscordBridgeComponent::connectConfiguredBot()
{
	if (configuredToken_.empty()) return false;
	return connectBot(configuredToken_, configuredIntents_);
}

void DiscordBridgeComponent::provideConfiguration(ILogger& logger, IEarlyConfig& config, bool defaults)
{
	(void)logger;
	if (defaults || config.getType("discord_bot_token") == ConfigOptionType_None)
	{
		config.setString("discord_bot_token", "");
	}
	if (defaults || config.getType("discord_bot_intents") == ConfigOptionType_None)
	{
		config.setInt("discord_bot_intents", DISCORD_DEFAULT_INTENTS);
	}
	if (defaults || config.getType("discord_batch_interval_ms") == ConfigOptionType_None)
	{
		config.setInt("discord_batch_interval_ms", DiscordMessageBatchConfig::DEFAULT_INTERVAL_MS);
	}
	if (defaults || config.getType("discord_batch_rate_limited") == ConfigOptionType_None)
	{
		config.setBool("discord_batch_rate_limited", false);
	}
	if (defaults || config.getType("discord_check_for_updates") == ConfigOptionType_None)
	{
		config.setBool("discord_check_for_updates", true);
	}

	if (defaults || config.getType("discord.channel_name") == ConfigOptionType_None)
	{
		config.setString("discord.channel_name", "");
	}
	if (defaults || config.getType("discord.channel_id") == ConfigOptionType_None)
	{
		config.setString("discord.channel_id", "");
	}
	// Accept the dotted spelling produced by nested config.json sections as an
	// alias of each flat key.
	config.addAlias("discord.bot_token", "discord_bot_token", true);
	config.addAlias("discord.intents", "discord_bot_intents", true);
	config.addAlias("discord.batch_interval_ms", "discord_batch_interval_ms", true);
	config.addAlias("discord.batch_rate_limited", "discord_batch_rate_limited", true);
	config.addAlias("discord.check_for_updates", "discord_check_for_updates", true);
	config.addAlias("discord_channel_name", "discord.channel_name", true);
	config.addAlias("discord_channel_id", "discord.channel_id", true);
}

void DiscordBridgeComponent::onFree(IComponent* component)
{
	if (pawn_ && component == pawn_)
	{
		pawn_->getEventDispatcher().removeEventHandler(this);
		pawn_ = nullptr;
	}
}

bool DiscordBridgeComponent::start(StringView token, int intents, StringView channelId, StringView channelName,
	int batchIntervalMs, bool batchRateLimited, bool checkForUpdates)
{
	sampMode_ = true;
	pawn_ = &sampPawn_;
	isInitialized_ = true;
	configuredChannelId_.assign(channelId.data(), channelId.length());
	configuredChannelName_.assign(channelName.data(), channelName.length());
	configuredToken_.assign(token.data(), token.length());
	configuredIntents_ = intents;
	configuredBatchIntervalMs_ = batchIntervalMs > 0 ? batchIntervalMs : DiscordMessageBatchConfig::DEFAULT_INTERVAL_MS;
	configuredBatchRateLimited_ = batchRateLimited;
	configuredUpdateCheck_ = checkForUpdates;
	configurationLoaded_ = true;
	startUpdateCheck();
	return connectConfiguredBot();
}

void DiscordBridgeComponent::free()
{
	if (core_)
	{
		core_->getEventDispatcher().removeEventHandler(this);
		core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
	}
	if (pawn_ && !sampMode_)
	{
		pawn_->getEventDispatcher().removeEventHandler(this);
	}
	if (sampMode_)
	{
		reset();
	}
	pawn_ = nullptr;

	instance_ = nullptr;
	delete this;
}

void DiscordBridgeComponent::reset()
{
	scriptsAwaitingReady_.clear();
	disconnectRequested_ = false;
	reconnectRequested_ = false;
	reconnectToken_.clear();
	ResetDiscordNativeHandles();

	channels_.clear();
	guilds_.clear();
	users_.clear();
	messages_.clear();
	roles_.clear();

	if (bot_)
	{
		bot_->disconnect();
		bot_.reset();
	}
	if (sampMode_)
	{
		sampPawn_.clear();
	}
}

IDiscordBot* DiscordBridgeComponent::getBot()
{
	return bot_.get();
}

bool DiscordBridgeComponent::connectBot(StringView token, int intents)
{
	if (bot_ && (bot_->isConnected() || bot_->isConnecting()))
	{
		return true;
	}

	// A second connection attempt may use a different token.  Do not expose
	// entities from the previous bot identity while the new gateway is still
	// loading its cache.
	if (bot_)
	{
		reset();
	}

	bot_ = std::make_unique<DiscordBot>(this, core_, token, intents);
	return bot_->connect();
}

IDiscordChannel* DiscordBridgeComponent::findConfiguredChannel()
{
	if (!configuredChannelId_.empty())
	{
		if (auto* channel = findChannelById(configuredChannelId_))
		{
			return channel;
		}
	}

	if (!configuredChannelName_.empty())
	{
		// This deliberately only searches the gateway cache. It is safe to call
		// from Pawn because it never performs a REST request or waits for I/O.
		return findChannelByName(configuredChannelName_);
	}

	return nullptr;
}

IDiscordChannel* DiscordBridgeComponent::findChannelById(StringView channelId)
{
	const std::string id(channelId.data(), channelId.length());
	auto it = channels_.find(id);
	return it != channels_.end() ? it->second.get() : nullptr;
}

IDiscordChannel* DiscordBridgeComponent::findChannelByName(StringView channelName)
{
	const std::string name(channelName.data(), channelName.length());
	for (auto& entry : channels_)
	{
		if (!entry.second)
		{
			continue;
		}
		const std::string candidate(entry.second->getChannelName().data(), entry.second->getChannelName().length());
		if (candidate == name)
		{
			return entry.second.get();
		}
	}

	// Name lookup is deliberately cache-only.  GUILD_CREATE supplies the
	// channel list, so this remains safe to call from a Pawn callback without a
	// blocking REST/DNS/TLS round trip on the server thread.
	return nullptr;
}

IDiscordGuild* DiscordBridgeComponent::findGuildById(StringView guildId)
{
	const std::string id(guildId.data(), guildId.length());
	auto it = guilds_.find(id);
	return it != guilds_.end() ? it->second.get() : nullptr;
}

IDiscordGuild* DiscordBridgeComponent::findGuildByName(StringView guildName)
{
	const std::string name(guildName.data(), guildName.length());
	for (auto& entry : guilds_)
	{
		if (!entry.second)
		{
			continue;
		}
		const std::string candidate(entry.second->getGuildName().data(), entry.second->getGuildName().length());
		if (candidate == name)
		{
			return entry.second.get();
		}
	}

	return nullptr;
}

IDiscordUser* DiscordBridgeComponent::findUserById(StringView userId)
{
	const std::string id(userId.data(), userId.length());
	auto it = users_.find(id);
	return it != users_.end() ? it->second.get() : nullptr;
}

IDiscordUser* DiscordBridgeComponent::findUserByName(StringView username)
{
	const std::string name(username.data(), username.length());
	for (auto& entry : users_)
	{
		if (!entry.second)
		{
			continue;
		}
		const std::string candidate(entry.second->getUsername().data(), entry.second->getUsername().length());
		if (candidate == name)
		{
			return entry.second.get();
		}
	}
	return nullptr;
}

IDiscordMessage* DiscordBridgeComponent::findMessageById(StringView messageId)
{
	auto it = messages_.find(std::string(messageId.data(), messageId.length()));
	return it != messages_.end() ? it->second.get() : nullptr;
}

IEventDispatcher<IDiscordEventHandler>& DiscordBridgeComponent::getEventDispatcher()
{
	return eventDispatcher_;
}

void DiscordBridgeComponent::onPlayerConnect(IPlayer& player)
{
	if (!queryExtension<IDiscordPlayerExtension>(player))
	{
		player.addExtension(new DiscordPlayerExtension(), true);
	}
}

void DiscordBridgeComponent::onAmxLoad(IPawnScript& script)
{
	RegisterDiscordNatives(script);
	queueReadyForScript(&script);
}

void DiscordBridgeComponent::onAmxUnload(IPawnScript& script)
{
	ForgetDiscordNativeScript(script);
}

void DiscordBridgeComponent::onAmxLoad(AMX* amx)
{
	sampPawn_.load(amx);
	queueReadyForScript(sampPawn_.getScript(amx));
}

void DiscordBridgeComponent::queueReadyForScript(IPawnScript* script)
{
	if (script && bot_ && bot_->isReady()) scriptsAwaitingReady_.push_back(script->GetID());
}

void DiscordBridgeComponent::deliverReadyToLateScripts()
{
	if (scriptsAwaitingReady_.empty() || !pawn_) return;
	const std::vector<int> pending = std::move(scriptsAwaitingReady_);
	scriptsAwaitingReady_.clear();
	if (!bot_ || !bot_->isReady()) return;

	auto deliver = [](IPawnScript* script)
	{
		int publicIndex = -1;
		if (!script || !script->IsLoaded() || script->FindPublic("DBR_OnReady", &publicIndex) != AMX_ERR_NONE ||
			publicIndex < 0 || publicIndex == INT_MAX) return;
		cell result = 1;
		const int error = script->CallChecked(publicIndex, result);
		if (error != AMX_ERR_NONE) script->PrintError(error);
	};
	for (const int id : pending)
	{
		if (IPawnScript* main = pawn_->mainScript(); main && main->GetID() == id)
		{
			deliver(main);
			continue;
		}
		for (IPawnScript* side : pawn_->sideScripts())
		{
			if (side && side->GetID() == id)
			{
				deliver(side);
				break;
			}
		}
	}
}

void DiscordBridgeComponent::onAmxUnload(AMX* amx)
{
	sampPawn_.unload(amx);
}

void DiscordBridgeComponent::onTick(Microseconds, TimePoint)
{
	DiscordLogFlush(core_);
	// ProcessTick is called from the server's C code: an exception escaping here
	// crashes the server (and shows up in whichever crash handler is installed,
	// such as FCNPC's).
	try
	{
		if (updateChecker_)
		{
			std::string latestVersion;
			if (updateChecker_->takeLatestVersion(latestVersion) && !latestVersion.empty())
			{
				DiscordLogMessage(core_, "[DiscordBridge] Update available: " + latestVersion + " (current v" +
					DISCORD_BRIDGE_VERSION + "). Download: https://github.com/itsneufox/omp-Discord-Bridge/releases/latest");
			}
		}
		ServiceDiscordNatives();
		deliverReadyToLateScripts();
		if (bot_)
		{
			insideBotUpdate_ = true;
			bot_->update();
			insideBotUpdate_ = false;
		}
		performPendingDisconnect();
	}
	catch (const std::exception& exception)
	{
		insideBotUpdate_ = false;
		DiscordLogWarning(core_, std::string("[DiscordBridge] tick failed: ") + exception.what());
	}
	catch (...)
	{
		insideBotUpdate_ = false;
		DiscordLogWarning(core_, "[DiscordBridge] tick failed with an unknown exception");
	}
}

DiscordBridgeComponent* DiscordBridgeComponent::getInstance()
{
	if (!instance_)
	{
		instance_ = new DiscordBridgeComponent();
	}
	return instance_;
}

void DiscordBridgeComponent::storeChannel(std::unique_ptr<DiscordChannel> channel)
{
	if (!channel)
	{
		return;
	}

	const std::string id(channel->getChannelId().data(), channel->getChannelId().length());
	if (id.empty()) return;
	channels_[id] = std::move(channel);
}

void DiscordBridgeComponent::storeGuild(std::unique_ptr<DiscordGuild> guild)
{
	if (!guild)
	{
		return;
	}

	const std::string id(guild->getGuildId().data(), guild->getGuildId().length());
	if (id.empty()) return;
	guilds_[id] = std::move(guild);
}

void DiscordBridgeComponent::storeUser(std::unique_ptr<DiscordUser> user)
{
	if (!user)
	{
		return;
	}

	const std::string id(user->getUserId().data(), user->getUserId().length());
	if (id.empty()) return;
	users_[id] = std::move(user);
}

void DiscordBridgeComponent::storeMessage(std::unique_ptr<DiscordMessage> message)
{
	if (!message)
	{
		return;
	}

	const std::string id(message->getMessageId().data(), message->getMessageId().length());
	if (id.empty()) return;
	messages_[id] = std::move(message);
}

void DiscordBridgeComponent::storeRole(std::unique_ptr<DiscordRole> role)
{
	if (!role)
	{
		return;
	}

	const std::string id(role->getRoleId().data(), role->getRoleId().length());
	if (id.empty()) return;
	roles_[id] = std::move(role);
}

DiscordChannel* DiscordBridgeComponent::upsertChannelFromJson(const std::string& json)
{
	const DiscordJson data = DiscordJson::parse(json, nullptr, false);
	if (data.is_discarded() || !data.is_object() || !(data.find("id") != data.end()) || !data["id"].is_string()) return nullptr;
	const std::string id = data["id"].get<std::string>();
	const auto it = channels_.find(id);
	if (it != channels_.end())
	{
		it->second->updateFromJson(json);
		if (!it->second->getGuildId().empty())
		{
			if (auto guild = static_cast<DiscordGuild*>(findGuildById(it->second->getGuildId()))) guild->addChannelId(it->second->getChannelId());
		}
		return it->second.get();
	}
	if (!bot_) return nullptr;
	auto channel = std::make_unique<DiscordChannel>(bot_.get(), id, jsonString(data, "name"), static_cast<EDiscordChannelType>(jsonInt(data, "type", 0)));
	channel->updateFromJson(json);
	DiscordChannel* raw = channel.get();
	storeChannel(std::move(channel));
	if (!raw->getGuildId().empty())
	{
		if (auto guild = static_cast<DiscordGuild*>(findGuildById(raw->getGuildId()))) guild->addChannelId(raw->getChannelId());
	}
	return raw;
}

DiscordGuild* DiscordBridgeComponent::upsertGuildFromJson(const std::string& json, bool includeMembers)
{
	const DiscordJson data = DiscordJson::parse(json, nullptr, false);
	if (data.is_discarded() || !data.is_object() || !(data.find("id") != data.end()) || !data["id"].is_string()) return nullptr;
	const std::string id = data["id"].get<std::string>();
	const auto it = guilds_.find(id);
	DiscordGuild* raw = nullptr;
	if (it != guilds_.end())
	{
		it->second->updateFromJson(json, includeMembers);
		raw = it->second.get();
	}
	else
	{
		if (!bot_) return nullptr;
		auto guild = std::make_unique<DiscordGuild>(bot_.get(), this, id, jsonString(data, "name"));
		guild->updateFromJson(json, includeMembers);
		raw = guild.get();
		storeGuild(std::move(guild));
	}

	if ((data.find("channels") != data.end()) && data["channels"].is_array())
	{
		for (const auto& channel : data["channels"])
		{
			if (channel.is_object()) upsertChannelFromJson(channel.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
		}
	}
	if ((data.find("roles") != data.end()) && data["roles"].is_array())
	{
		for (const auto& role : data["roles"])
		{
			if (role.is_object()) upsertRoleFromJson(role.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), id);
		}
	}
	if (includeMembers && (data.find("members") != data.end()) && data["members"].is_array())
	{
		for (const auto& member : data["members"])
		{
			if (!member.is_object()) continue;
			if ((member.find("user") != member.end()) && member["user"].is_object()) upsertUserFromJson(member["user"].dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
		}
	}
	if ((data.find("presences") != data.end()) && data["presences"].is_array())
	{
		for (const auto& presence : data["presences"])
		{
			if (!presence.is_object()) continue;
			const DiscordJson user = presence.value("user", DiscordJson::object());
			if (user.is_object()) upsertUserFromJson(user.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
		}
	}
	return raw;
}

DiscordUser* DiscordBridgeComponent::upsertUserFromJson(const std::string& json)
{
	const DiscordJson data = DiscordJson::parse(json, nullptr, false);
	if (data.is_discarded() || !data.is_object() || !(data.find("id") != data.end()) || !data["id"].is_string()) return nullptr;
	const std::string id = data["id"].get<std::string>();
	const auto it = users_.find(id);
	if (it != users_.end())
	{
		it->second->updateFromJson(json);
		return it->second.get();
	}
	auto user = std::make_unique<DiscordUser>(id, jsonString(data, "username"), jsonString(data, "discriminator"), jsonBool(data, "bot", false));
	user->updateFromJson(json);
	DiscordUser* raw = user.get();
	storeUser(std::move(user));
	return raw;
}

DiscordUser* DiscordBridgeComponent::findUserByNameAndDiscriminator(StringView username, StringView discriminator)
{
	const std::string name(username.data(), username.length());
	const std::string disc(discriminator.data(), discriminator.length());
	for (auto& entry : users_)
	{
		if (!entry.second) continue;
		const std::string candidateName(entry.second->getUsername().data(), entry.second->getUsername().length());
		const std::string candidateDisc(entry.second->getDiscriminator().data(), entry.second->getDiscriminator().length());
		if (candidateName == name && (disc.empty() || candidateDisc == disc)) return entry.second.get();
	}
	return nullptr;
}

DiscordMessage* DiscordBridgeComponent::upsertMessageFromJson(const std::string& json)
{
	const DiscordJson data = DiscordJson::parse(json, nullptr, false);
	if (data.is_discarded() || !data.is_object() || !(data.find("id") != data.end()) || !data["id"].is_string()) return nullptr;
	const std::string id = data["id"].get<std::string>();
	if ((data.find("author") != data.end()) && data["author"].is_object()) upsertUserFromJson(data["author"].dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
	const std::string channelId = jsonString(data, "channel_id");
	const std::string authorId = (data.find("author") != data.end()) && data["author"].is_object() ? jsonString(data["author"], "id") : std::string();
	const auto it = messages_.find(id);
	if (it != messages_.end())
	{
		it->second->updateFromJson(json);
		return it->second.get();
	}
	if (!bot_) return nullptr;
	auto message = std::make_unique<DiscordMessage>(bot_.get(), id, channelId, authorId, jsonString(data, "content"));
	message->updateFromJson(json);
	DiscordMessage* raw = message.get();
	storeMessage(std::move(message));
	return raw;
}

DiscordRole* DiscordBridgeComponent::upsertRoleFromJson(const std::string& json, StringView guildId)
{
	const DiscordJson data = DiscordJson::parse(json, nullptr, false);
	if (data.is_discarded() || !data.is_object() || !(data.find("id") != data.end()) || !data["id"].is_string()) return nullptr;
	const std::string id = data["id"].get<std::string>();
	const auto it = roles_.find(id);
	if (it != roles_.end())
	{
		it->second->updateFromJson(json);
		if (!guildId.empty()) it->second->setGuildId(guildId);
		if (!it->second->getGuildId().empty())
		{
			if (auto guild = static_cast<DiscordGuild*>(findGuildById(it->second->getGuildId()))) guild->addRoleId(it->second->getRoleId());
		}
		return it->second.get();
	}
	auto role = std::make_unique<DiscordRole>(id, jsonString(data, "name"));
	role->updateFromJson(json);
	if (!guildId.empty()) role->setGuildId(guildId);
	DiscordRole* raw = role.get();
	storeRole(std::move(role));
	if (!raw->getGuildId().empty())
	{
		if (auto guild = static_cast<DiscordGuild*>(findGuildById(raw->getGuildId()))) guild->addRoleId(raw->getRoleId());
	}
	return raw;
}

DiscordRole* DiscordBridgeComponent::findRoleByIdInternal(StringView roleId)
{
	auto it = roles_.find(std::string(roleId.data(), roleId.length()));
	return it != roles_.end() ? it->second.get() : nullptr;
}

DiscordRole* DiscordBridgeComponent::findRoleByNameInternal(StringView roleName, StringView guildId)
{
	const std::string name(roleName.data(), roleName.length());
	const std::string guild(guildId.data(), guildId.length());
	for (auto& entry : roles_)
	{
		if (!entry.second)
		{
			continue;
		}
		const std::string candidate(entry.second->getRoleName().data(), entry.second->getRoleName().length());
		const std::string candidateGuild(entry.second->getGuildId().data(), entry.second->getGuildId().length());
		if (candidate == name && (guild.empty() || candidateGuild == guild))
		{
			return entry.second.get();
		}
	}
	return nullptr;
}

void DiscordBridgeComponent::removeChannel(StringView channelId)
{
	channels_.erase(std::string(channelId.data(), channelId.length()));
}

void DiscordBridgeComponent::removeGuild(StringView guildId)
{
	guilds_.erase(std::string(guildId.data(), guildId.length()));
}

void DiscordBridgeComponent::removeUser(StringView userId)
{
	users_.erase(std::string(userId.data(), userId.length()));
}

void DiscordBridgeComponent::removeMessage(StringView messageId)
{
	messages_.erase(std::string(messageId.data(), messageId.length()));
}

void DiscordBridgeComponent::removeRole(StringView roleId)
{
	roles_.erase(std::string(roleId.data(), roleId.length()));
}

std::vector<std::string> DiscordBridgeComponent::getKnownGuildIds() const
{
	std::vector<std::string> ids;
	ids.reserve(guilds_.size());
	for (const auto& entry : guilds_)
	{
		ids.push_back(entry.first);
	}
	return ids;
}

void DiscordBridgeComponent::onChannelCreateEvent(DiscordChannel& channel)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onChannelCreate, channel);
	const cell channelHandle = GetOrCreateDiscordChannelHandle(channel.getChannelId());
	CallPawnPublic(pawn_, "DBR_OnChannelCreate", channelHandle);
}

void DiscordBridgeComponent::onChannelUpdateEvent(DiscordChannel& channel)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onChannelUpdate, channel);
	const cell channelHandle = GetOrCreateDiscordChannelHandle(channel.getChannelId());
	CallPawnPublic(pawn_, "DBR_OnChannelUpdate", channelHandle);
}

void DiscordBridgeComponent::onChannelDeleteEvent(DiscordChannel& channel)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onChannelDelete, channel);
	const cell channelHandle = GetOrCreateDiscordChannelHandle(channel.getChannelId());
	CallPawnPublic(pawn_, "DBR_OnChannelDelete", channelHandle);
}

void DiscordBridgeComponent::onMessageCreateEvent(DiscordMessage& message)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onMessageCreate, message);
	const cell msgHandle = GetOrCreateDiscordMessageHandle(message.getMessageId(), message.getChannelId());
	CallPawnPublic(pawn_, "DBR_OnMessageCreate", msgHandle);
}

void DiscordBridgeComponent::onMessageUpdateEvent(DiscordMessage& message)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onMessageUpdate, message);
	CallPawnPublic(pawn_, "DBR_OnMessageUpdate", GetOrCreateDiscordMessageHandle(message.getMessageId(), message.getChannelId()));
}

void DiscordBridgeComponent::onMessageDeleteEvent(DiscordMessage& message)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onMessageDelete, message.getChannelId(), message.getMessageId());
	const cell msgHandle = GetOrCreateDiscordMessageHandle(message.getMessageId(), message.getChannelId());
	CallPawnPublic(pawn_, "DBR_OnMessageDelete", msgHandle);
}

void DiscordBridgeComponent::onBotReadyEvent()
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onBotReady);
	CallPawnPublic(pawn_, "DBR_OnReady");
	// Publish the application commands scripts created during startup.
	NotifyDiscordNativesReady();
}

void DiscordBridgeComponent::onBotDisconnectedEvent()
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onBotDisconnected);
	CallPawnPublic(pawn_, "DBR_OnDisconnected");
}

void DiscordBridgeComponent::onGuildCreateEvent(DiscordGuild& guild)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onGuildCreate, guild);
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	CallPawnPublic(pawn_, "DBR_OnGuildCreate", guildHandle);
}

void DiscordBridgeComponent::onGuildUpdateEvent(DiscordGuild& guild)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onGuildUpdate, guild);
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	CallPawnPublic(pawn_, "DBR_OnGuildUpdate", guildHandle);
}

void DiscordBridgeComponent::onGuildDeleteEvent(StringView guildId)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onGuildDelete, guildId);
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guildId);
	CallPawnPublic(pawn_, "DBR_OnGuildDelete", guildHandle);
}

void DiscordBridgeComponent::onUserUpdateEvent(DiscordUser& user)
{
	const cell userHandle = GetOrCreateDiscordUserHandle(user.getUserId());
	CallPawnPublic(pawn_, "DBR_OnUserUpdate", userHandle);
}

void DiscordBridgeComponent::onGuildMemberAddEvent(DiscordGuild& guild, DiscordUser& user)
{
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	const cell userHandle = GetOrCreateDiscordUserHandle(user.getUserId());
	CallPawnPublic(pawn_, "DBR_OnGuildMemberAdd", guildHandle, userHandle);
}

void DiscordBridgeComponent::onGuildMemberUpdateEvent(DiscordGuild& guild, DiscordUser& user)
{
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	const cell userHandle = GetOrCreateDiscordUserHandle(user.getUserId());
	CallPawnPublic(pawn_, "DBR_OnGuildMemberUpdate", guildHandle, userHandle);
}

void DiscordBridgeComponent::onGuildMemberVoiceUpdateEvent(DiscordGuild& guild, DiscordUser& user, DiscordChannel* channel)
{
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	const cell userHandle = GetOrCreateDiscordUserHandle(user.getUserId());
	const cell channelHandle = channel ? GetOrCreateDiscordChannelHandle(channel->getChannelId()) : 0;
	CallPawnPublic(pawn_, "DBR_OnGuildMemberVoiceUpdate", guildHandle, userHandle, channelHandle);
}

void DiscordBridgeComponent::onGuildMemberRemoveEvent(DiscordGuild& guild, DiscordUser& user)
{
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	const cell userHandle = GetOrCreateDiscordUserHandle(user.getUserId());
	CallPawnPublic(pawn_, "DBR_OnGuildMemberRemove", guildHandle, userHandle);
}

void DiscordBridgeComponent::onGuildRoleCreateEvent(DiscordGuild& guild, DiscordRole& role)
{
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	const cell roleHandle = GetOrCreateDiscordRoleHandle(role.getRoleId());
	CallPawnPublic(pawn_, "DBR_OnGuildRoleCreate", guildHandle, roleHandle);
}

void DiscordBridgeComponent::onGuildRoleUpdateEvent(DiscordGuild& guild, DiscordRole& role)
{
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	const cell roleHandle = GetOrCreateDiscordRoleHandle(role.getRoleId());
	CallPawnPublic(pawn_, "DBR_OnGuildRoleUpdate", guildHandle, roleHandle);
}

void DiscordBridgeComponent::onGuildRoleDeleteEvent(DiscordGuild& guild, DiscordRole& role)
{
	const cell guildHandle = GetOrCreateDiscordGuildHandle(guild.getGuildId());
	const cell roleHandle = GetOrCreateDiscordRoleHandle(role.getRoleId());
	CallPawnPublic(pawn_, "DBR_OnGuildRoleDelete", guildHandle, roleHandle);
}

void DiscordBridgeComponent::onMessageReactionEvent(DiscordMessage& message, DiscordUser* reactionUser, cell emojiHandle, StringView emojiToken, int reactionType)
{
	eventDispatcher_.dispatch(&IDiscordEventHandler::onMessageReaction, message, reactionUser, emojiToken, reactionType);
	const cell messageHandle = GetOrCreateDiscordMessageHandle(message.getMessageId(), message.getChannelId());
	const cell userHandle = reactionUser ? GetOrCreateDiscordUserHandle(reactionUser->getUserId()) : 0;
	CallPawnPublic(pawn_, "DBR_OnMessageReaction", messageHandle, userHandle, emojiHandle, reactionType);
}

DiscordBridgeComponent::~DiscordBridgeComponent()
{
	reset();
}
