/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#pragma once

#include "discord-interface.hpp"
#include "discord-bot.hpp"
#include "discord-message-batch-config.hpp"
#include "discord-update-checker.hpp"
#include "samp-pawn.hpp"
#include <Server/Components/Pawn/pawn.hpp>
#include <Impl/pool_impl.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

using Impl::DefaultEventDispatcher;

class DiscordChannel;
class DiscordGuild;
class DiscordUser;
class DiscordMessage;
class DiscordRole;

class DiscordBridgeComponent final
	: public IDiscordBridgeComponent
	, public PlayerConnectEventHandler
	, public PawnEventHandler
	, public CoreEventHandler
{
private:
	ICore* core_ = nullptr;
	IPawnComponent* pawn_ = nullptr;
	SampPawnComponent sampPawn_;
	bool sampMode_ = false;

	std::unique_ptr<DiscordBot> bot_;

	std::unordered_map<std::string, std::unique_ptr<DiscordChannel>> channels_;
	std::unordered_map<std::string, std::unique_ptr<DiscordGuild>> guilds_;
	std::unordered_map<std::string, std::unique_ptr<DiscordUser>> users_;
	std::unordered_map<std::string, std::unique_ptr<DiscordMessage>> messages_;
	std::unordered_map<std::string, std::unique_ptr<DiscordRole>> roles_;

	DefaultEventDispatcher<IDiscordEventHandler> eventDispatcher_;

	static DiscordBridgeComponent* instance_;

	bool isInitialized_ = false;
	std::string configuredChannelId_;
	std::string configuredChannelName_;
	// Token from DISCORD_BOT_TOKEN or the server configuration.  It takes
	// priority over DBR_ConnectBot.
	std::string configuredToken_;
	int configuredIntents_ = DISCORD_DEFAULT_INTENTS;
	int configuredBatchIntervalMs_ = DiscordMessageBatchConfig::DEFAULT_INTERVAL_MS;
	bool configuredBatchRateLimited_ = false;
	bool configuredUpdateCheck_ = true;
	bool configurationLoaded_ = false;
	std::unique_ptr<DiscordUpdateChecker> updateChecker_;
	// DBR_DisconnectBot can run inside a callback dispatched by the bot's own
	// update().  Destroying the bot there would free the object that is still
	// running, so in that case the disconnect waits until update() returns.
	bool insideBotUpdate_ = false;
	// Scripts loaded after the bot became ready (SA-MP loads the gamemode after
	// the plugin connects; filterscripts can be reloaded any time) still get
	// DBR_OnReady, on the next tick so their init callbacks run first.
	std::vector<int> scriptsAwaitingReady_;
	void startUpdateCheck();
	void queueReadyForScript(IPawnScript* script);
	void deliverReadyToLateScripts();
	bool disconnectRequested_ = false;
	bool reconnectRequested_ = false;
	std::string reconnectToken_;
	int reconnectIntents_ = DISCORD_DEFAULT_INTENTS;

public:
	StringView componentName() const override;
	SemanticVersion componentVersion() const override;
	void onLoad(ICore* c) override;
	void onInit(IComponentList* components) override;
	void onReady() override;
	void onFree(IComponent* component) override;
	void provideConfiguration(ILogger& logger, IEarlyConfig& config, bool defaults) override;
	void free() override;
	void reset() override;

	IDiscordBot* getBot() override;
	bool connectBot(StringView token, int intents) override;
	void loadConfiguration();
	bool hasConfiguredToken() const { return !configuredToken_.empty(); }
	bool connectConfiguredBot();
	bool requestDisconnect();
	bool isDisconnectPending() const { return disconnectRequested_; }
	void queueReconnect(StringView token, int intents);
	void performPendingDisconnect();
	IDiscordChannel* findConfiguredChannel();
	StringView configuredChannelId() const { return StringView(configuredChannelId_); }

	IDiscordChannel* findChannelById(StringView channelId) override;
	IDiscordChannel* findChannelByName(StringView channelName) override;
	IDiscordGuild* findGuildById(StringView guildId) override;
	IDiscordGuild* findGuildByName(StringView guildName) override;
	IDiscordUser* findUserById(StringView userId) override;
	IDiscordUser* findUserByName(StringView username) override;
	IDiscordMessage* findMessageById(StringView messageId) override;

	IEventDispatcher<IDiscordEventHandler>& getEventDispatcher() override;

	void onPlayerConnect(IPlayer& player) override;

	void onAmxLoad(IPawnScript& script) override;
	void onAmxUnload(IPawnScript& script) override;
	void onAmxLoad(AMX* amx);
	void onAmxUnload(AMX* amx);

	void onTick(Microseconds elapsed, TimePoint now) override;

	int getMessageBatchInterval() { loadConfiguration(); return configuredBatchIntervalMs_; }
	bool batchRateLimitedMessages() { loadConfiguration(); return configuredBatchRateLimited_; }
	bool start(StringView token, int intents, StringView channelId = {}, StringView channelName = {},
		int batchIntervalMs = DiscordMessageBatchConfig::DEFAULT_INTERVAL_MS, bool batchRateLimited = false,
		bool checkForUpdates = true);

	static DiscordBridgeComponent* getInstance();

	ICore* getCore() const { return core_; }
	void storeChannel(std::unique_ptr<DiscordChannel> channel);
	void storeGuild(std::unique_ptr<DiscordGuild> guild);
	void storeUser(std::unique_ptr<DiscordUser> user);
	void storeMessage(std::unique_ptr<DiscordMessage> message);
	void storeRole(std::unique_ptr<DiscordRole> role);
	DiscordChannel* upsertChannelFromJson(const std::string& json);
	DiscordGuild* upsertGuildFromJson(const std::string& json, bool includeMembers = true);
	DiscordUser* upsertUserFromJson(const std::string& json);
	DiscordUser* findUserByNameAndDiscriminator(StringView username, StringView discriminator);
	DiscordMessage* upsertMessageFromJson(const std::string& json);
	DiscordRole* upsertRoleFromJson(const std::string& json, StringView guildId = {});
	DiscordRole* findRoleByIdInternal(StringView roleId);
	DiscordRole* findRoleByNameInternal(StringView roleName, StringView guildId = {});

	void removeChannel(StringView channelId);
	void removeGuild(StringView guildId);
	void removeUser(StringView userId);
	void removeMessage(StringView messageId);
	void removeRole(StringView roleId);
	std::vector<std::string> getKnownGuildIds() const;

	void onChannelCreateEvent(DiscordChannel& channel);
	void onChannelUpdateEvent(DiscordChannel& channel);
	void onChannelDeleteEvent(DiscordChannel& channel);
	void onMessageCreateEvent(DiscordMessage& message);
	void onMessageUpdateEvent(DiscordMessage& message);
	void onMessageDeleteEvent(DiscordMessage& message);
	void onBotReadyEvent();
	void onBotDisconnectedEvent();
	void onGuildCreateEvent(DiscordGuild& guild);
	void onGuildUpdateEvent(DiscordGuild& guild);
	void onGuildDeleteEvent(StringView guildId);
	void onUserUpdateEvent(DiscordUser& user);
	void onGuildMemberAddEvent(DiscordGuild& guild, DiscordUser& user);
	void onGuildMemberUpdateEvent(DiscordGuild& guild, DiscordUser& user);
	void onGuildMemberVoiceUpdateEvent(DiscordGuild& guild, DiscordUser& user, DiscordChannel* channel);
	void onGuildMemberRemoveEvent(DiscordGuild& guild, DiscordUser& user);
	void onGuildRoleCreateEvent(DiscordGuild& guild, DiscordRole& role);
	void onGuildRoleUpdateEvent(DiscordGuild& guild, DiscordRole& role);
	void onGuildRoleDeleteEvent(DiscordGuild& guild, DiscordRole& role);
	void onMessageReactionEvent(DiscordMessage& message, DiscordUser* reactionUser, cell emojiHandle, StringView emojiToken, int reactionType);
	IPawnComponent* getPawnComponent() { return pawn_; }
	const IPawnComponent* getPawnComponent() const { return pawn_; }

	~DiscordBridgeComponent();
};

class DiscordPlayerExtension : public IDiscordPlayerExtension
{
private:
	std::string linkedDiscordId_;

public:
	void setLinkedDiscordId(StringView discordId) override
	{
		linkedDiscordId_ = std::string(discordId.data(), discordId.length());
	}

	StringView getLinkedDiscordId() const override
	{
		return StringView(linkedDiscordId_);
	}

	bool isLinked() const override
	{
		return !linkedDiscordId_.empty();
	}

	void freeExtension() override
	{
		delete this;
	}

	void reset() override
	{
		linkedDiscordId_.clear();
	}
};
