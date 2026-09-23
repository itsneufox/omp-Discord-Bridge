/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#include "discord-bot.hpp"
#include "discord-component.hpp"
#include "discord-log.hpp"
#include "discord-json.hpp"
#include "discord-channel.hpp"
#include "discord-guild.hpp"
#include "discord-user.hpp"
#include "discord-message.hpp"
#include "discord-role.hpp"
#include "natives.hpp"
#include "utils.hpp"
#include <algorithm>
#include <chrono>
#include <exception>

namespace
{
bool retryableStartupResponse(const DiscordHTTP::Response& response)
{
	return response.statusCode == 0 || response.statusCode == 429 || response.statusCode >= 500;
}

bool waitForStartupRetry(const std::atomic<bool>& shouldStop, unsigned seconds)
{
	for (unsigned elapsed = 0; elapsed < seconds; ++elapsed)
	{
		if (shouldStop.load()) return false;
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	return !shouldStop.load();
}

unsigned startupRetryDelay(unsigned attempt)
{
	return std::min(60U, 1U << std::min(attempt, 5U));
}

void logStartupFailure(ICore* core, const std::string& token, const char* stage, const DiscordHTTP::Response& response)
{
	std::string detail = response.body;
	if (detail.empty())
	{
		detail = response.statusCode == 0 ? "network or TLS failure" : "request rejected";
	}
	if (!token.empty())
	{
		size_t position = 0;
		while ((position = detail.find(token, position)) != std::string::npos)
		{
			detail.replace(position, token.size(), "<redacted>");
			position += 10;
		}
	}
	for (char& character : detail)
	{
		if (character == '\r' || character == '\n') character = ' ';
	}
	if (detail.size() > 180) detail.resize(180);

	DiscordLogWarning(core, std::string("[DiscordBridge] Discord ") + stage +
		" failed (HTTP " + std::to_string(response.statusCode) + "): " + detail);
}

DiscordGuild* findOrCreateGuild(DiscordBridgeComponent* component, const std::string& id)
{
	if (!component || id.empty()) return nullptr;
	// Gateway member/presence/role events are meaningful only for a guild that
	// has already arrived through GUILD_CREATE.  Creating an empty placeholder
	// here would fire delete/update callbacks for objects scripts never saw.
	return static_cast<DiscordGuild*>(component->findGuildById(id));
}
}

DiscordBot::DiscordBot(DiscordBridgeComponent* component, ICore* core, StringView token, int intents)
	: component_(component)
	, core_(core)
	, botToken_(token.data(), token.length())
	, intents_(intents)
	, connected_(false)
	, connecting_(false)
	, shouldStop_(false)
{
}

DiscordBot::~DiscordBot()
{
	stop();
}

StringView DiscordBot::getBotId() const
{
	return StringView(botId_);
}

StringView DiscordBot::getBotUsername() const
{
	return StringView(botUsername_);
}

bool DiscordBot::isConnected() const
{
	return connected_.load();
}

bool DiscordBot::setPresenceStatus(EDiscordPresenceStatus status)
{
	if (!websocket_) return false;
	const int value = static_cast<int>(status);
	const bool sent = websocket_->sendPresenceUpdate(value, activityType_, activityName_, activityUrl_);
	if (sent) presenceStatus_ = value;
	return sent;
}

bool DiscordBot::setActivity(EDiscordActivityType type, StringView name)
{
	return setActivity(static_cast<int>(type), name, StringView());
}

bool DiscordBot::setActivity(int type, StringView name, StringView url)
{
	if (!websocket_ || type < 0 || type > 5) return false;
	const std::string activityName(name.data(), name.length());
	const std::string activityUrl(url.data(), url.length());
	if (!websocket_->sendPresenceUpdate(presenceStatus_.load(), type, activityName, activityUrl)) return false;
	activityType_ = type;
	activityName_ = activityName;
	activityUrl_ = activityUrl;
	return true;
}

bool DiscordBot::disconnect()
{
	stop();
	return true;
}

bool DiscordBot::reconnect()
{
	stop();
	return connect();
}

bool DiscordBot::connect()
{
	if (botToken_.empty()) return false;
	if (connectThread_.joinable()) connectThread_.join();

	http_ = std::make_unique<DiscordHTTP>(core_, botToken_);
	websocket_ = std::make_unique<DiscordWebSocket>(this, core_, botToken_, intents_);
	websocket_->setMessageCallback([this](const std::string& payload)
	{
		enqueueGatewayMessage(payload);
	});
	shouldStop_ = false;
	restQueue_.reset();
	restStop_ = false;
	connected_ = false;
	connecting_ = true;
	lastGatewayConnected_ = false;
	initialGuildIds_.clear();
	memberSyncRequested_.clear();
	memberSyncRetryAt_.clear();
	initialGuildSync_ = false;
	readyEventSent_ = false;
	connectThread_ = std::thread([this]()
	{
		try
		{
			initializeConnection();
		}
		catch (const std::exception& exception)
		{
			DiscordLogWarning(core_, std::string("[DiscordBridge] Discord connection failed: ") + exception.what());
			connected_ = false;
			connecting_ = false;
		}
		catch (...)
		{
			DiscordLogWarning(core_, "[DiscordBridge] Discord connection failed with an unknown exception");
			connected_ = false;
			connecting_ = false;
		}
	});
	return true;
}

void DiscordBot::initializeConnection()
{
	DiscordHTTP::Response botUser {};
	bool loggedBotUserFailure = false;
	for (unsigned attempt = 0; ; ++attempt)
	{
		botUser = http_->getBotUser();
		if (botUser.success) break;
		if (!loggedBotUserFailure)
		{
			logStartupFailure(core_, botToken_, "bot identity request", botUser);
			loggedBotUserFailure = true;
		}
		if (!retryableStartupResponse(botUser) || !waitForStartupRetry(shouldStop_, startupRetryDelay(attempt)))
		{
			connected_ = false;
			connecting_ = false;
			// Startup failed, but natives may already have queued work.  Run it so
			// each one fails visibly through its callback and DBR_OnActionFail,
			// instead of waiting for a connection that is not coming.
			startRestWorker();
			return;
		}
	}

	const std::string botId = DiscordUtils::extractJsonString(botUser.body, "id");
	if (botId.empty() || shouldStop_)
	{
		connected_ = false;
		connecting_ = false;
		if (!shouldStop_) startRestWorker();
		return;
	}
	http_->setApplicationId(botId);

	DiscordHTTP::Response gateway {};
	bool loggedGatewayFailure = false;
	for (unsigned attempt = 0; ; ++attempt)
	{
		gateway = http_->getGatewayBot();
		if (gateway.success) break;
		if (!loggedGatewayFailure)
		{
			logStartupFailure(core_, botToken_, "gateway discovery request", gateway);
			loggedGatewayFailure = true;
		}
		if (!retryableStartupResponse(gateway) || !waitForStartupRetry(shouldStop_, startupRetryDelay(attempt)))
		{
			connected_ = false;
			connecting_ = false;
			// Startup failed, but natives may already have queued work.  Run it so
			// each one fails visibly through its callback and DBR_OnActionFail,
			// instead of waiting for a connection that is not coming.
			startRestWorker();
			return;
		}
	}
	if (gateway.success)
	{
		const std::string gatewayUrl = DiscordUtils::extractJsonString(gateway.body, "url");
		if (!gatewayUrl.empty()) websocket_->setGatewayUrl(gatewayUrl);
	}
	if (shouldStop_)
	{
		connected_ = false;
		connecting_ = false;
		return;
	}
	// Start queued REST work only after authentication has supplied the
	// application id.  Natives may enqueue work during the handshake; it will
	// wait here instead of racing command/interaction endpoints with startup.
	startRestWorker();
	const bool started = websocket_->connect();
	if (!started)
	{
		connected_ = false;
		connecting_ = false;
	}
}

void DiscordBot::stop()
{
	shouldStop_ = true;
	restStop_ = true;
	restQueue_.stop();
	connected_ = false;
	// The REST handshake runs on its own thread.  Wait for it before touching
	// the websocket object so shutdown cannot race its final connect call.
	if (connectThread_.joinable()) connectThread_.join();
	// Close the gateway before waiting for REST work, so the bot goes offline
	// without waiting for an HTTP request that is still in flight.
	if (websocket_) websocket_->disconnect();
	if (restThread_.joinable()) restThread_.join();
	connecting_ = false;
	{
		std::lock_guard<std::mutex> lock(gatewayEventsMutex_);
		gatewayEvents_.clear();
	}

	{
		std::lock_guard<std::mutex> lock(completionTasksMutex_);
		completionTasks_.clear();
	}
	initialGuildIds_.clear();
	memberSyncRequested_.clear();
	memberSyncRetryAt_.clear();
	initialGuildSync_ = false;
	readyEventSent_ = false;
}

void DiscordBot::update()
{
	const auto started = std::chrono::steady_clock::now();
	constexpr size_t MAX_COMPLETIONS_PER_TICK = 64;
	for (size_t processed = 0; processed < MAX_COMPLETIONS_PER_TICK; ++processed)
	{
		std::function<void()> task;
		{
			std::lock_guard<std::mutex> lock(completionTasksMutex_);
			if (completionTasks_.empty()) break;
			task = std::move(completionTasks_.front());
			completionTasks_.pop_front();
		}
		if (!shouldStop_ && task)
		{
			try
			{
				task();
			}
			catch (const std::exception& exception)
			{
				DiscordLogWarning(core_, std::string("[DiscordBridge] Discord request completion failed: ") + exception.what());
			}
			catch (...)
			{
				DiscordLogWarning(core_, "[DiscordBridge] Discord request completion failed with an unknown exception");
			}
		}
		if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(4)) break;
	}

	if (websocket_)
	{
		const bool gatewayConnected = websocket_->isConnected();
		const bool wasConnected = connected_.load();
		if (!gatewayConnected) connected_ = false;
		if (!gatewayConnected && !shouldStop_) connecting_ = true;
		if (lastGatewayConnected_ && !gatewayConnected && wasConnected)
		{
			component_->onBotDisconnectedEvent();
		}
		lastGatewayConnected_ = gatewayConnected;
	}
	serviceMemberSyncRetries();
	// Gateway I/O is off-thread, but entity mutation and Pawn callbacks must
	// happen on the component tick.  Bound the amount of queued work per tick
	// so a reconnect burst or member-chunk burst cannot monopolize the game
	// loop.  Anything left in the queue is handled on the next tick.
	constexpr size_t MAX_EVENTS_PER_TICK = 64;
	constexpr auto MAX_TICK_BUDGET = std::chrono::milliseconds(4);
	for (size_t processed = 0; processed < MAX_EVENTS_PER_TICK; ++processed)
	{
		std::string payload;
		{
			std::lock_guard<std::mutex> lock(gatewayEventsMutex_);
			if (gatewayEvents_.empty()) break;
			payload = std::move(gatewayEvents_.front());
			gatewayEvents_.pop_front();
		}
		if (!shouldStop_)
		{
			// One malformed or unexpected event must not stop the others.
			try
			{
				handleGatewayMessage(payload);
			}
			catch (const std::exception& exception)
			{
				DiscordLogWarning(core_, std::string("[DiscordBridge] Discord gateway event failed: ") + exception.what());
			}
			catch (...)
			{
				DiscordLogWarning(core_, "[DiscordBridge] Discord gateway event failed with an unknown exception");
			}
		}
		if (processed > 0 && std::chrono::steady_clock::now() - started >= MAX_TICK_BUDGET) break;
	}
}

bool DiscordBot::sendChannelMessage(const std::string& channelId, const std::string& message,
	std::function<void(const DiscordHTTP::Response&)> completion)
{
	if (shouldStop_ || restStop_ || !http_) return false;
	const int interval = !completion && component_->batchRateLimitedMessages() ? component_->getMessageBatchInterval() : 0;
	return restQueue_.pushMessage(channelId, message, interval,
		[this, channelId, completion = std::move(completion)](const std::string& content, unsigned& retries, DiscordRateLimits::Time eligibleAt)
	{
		if (shouldStop_ || !http_) return;
		http_->runTask([&](DiscordHTTP& http)
		{
			const auto response = http.sendMessage(channelId, content);
			if (completion) completion(response);
		}, retries, eligibleAt);
	}, [this]()
	{
		DiscordLogWarning(core_, "[DiscordBridge] Discord REST queue limit reached; request dropped");
	});
}

bool DiscordBot::submitRestTask(std::function<void(DiscordHTTP&)> task)
{
	if (!task || shouldStop_ || restStop_ || !http_) return false;
	return restQueue_.push([this, task = std::move(task)](unsigned& retries, DiscordRateLimits::Time eligibleAt)
	{
		if (!shouldStop_ && http_) http_->runTask(task, retries, eligibleAt);
	}, [this]()
	{
		DiscordLogWarning(core_, "[DiscordBridge] Discord REST queue limit reached; request dropped");
	});
}

void DiscordBot::enqueueCompletion(std::function<void()> task)
{
	if (!task || shouldStop_) return;
	std::lock_guard<std::mutex> lock(completionTasksMutex_);
	if (completionTasks_.size() >= 4096)
	{
		if (!completionQueueLimitLogged_)
		{
			DiscordLogWarning(core_, "[DiscordBridge] Discord completion queue limit reached; result dropped");
			completionQueueLimitLogged_ = true;
		}
		return;
	}
	if (completionTasks_.size() < 2048) completionQueueLimitLogged_ = false;
	completionTasks_.push_back(std::move(task));
}

void DiscordBot::startRestWorker()
{
	if (restThread_.joinable() || restStop_ || shouldStop_) return;
	restThread_ = std::thread(&DiscordBot::runRestTasks, this);
}

void DiscordBot::runRestTasks()
{
	restQueue_.run([this](const std::exception& exception)
	{
		DiscordLogWarning(core_, std::string("[DiscordBridge] Discord REST task failed: ") + exception.what());
	});
}

void DiscordBot::serviceMemberSyncRetries()
{
	if (shouldStop_ || !websocket_) return;

	const auto now = std::chrono::steady_clock::now();
	for (auto it = memberSyncRetryAt_.begin(); it != memberSyncRetryAt_.end();)
	{
		if (it->second > now)
		{
			++it;
			continue;
		}

		const std::string guildId = it->first;
		it = memberSyncRetryAt_.erase(it);
		const auto* guild = static_cast<const DiscordGuild*>(component_->findGuildById(guildId));
		if (!guild || guild->getMemberCount() <= static_cast<int>(guild->getMemberIds().size())) continue;
		if (memberSyncRequested_.insert(guildId).second) websocket_->requestGuildMembers(guildId);
	}
}

void DiscordBot::enqueueGatewayMessage(const std::string& payload)
{
	if (shouldStop_) return;
	std::lock_guard<std::mutex> lock(gatewayEventsMutex_);
	if (gatewayEvents_.size() >= 4096)
	{
		if (!gatewayQueueLimitLogged_)
		{
			DiscordLogWarning(core_, "[DiscordBridge] Discord gateway event queue limit reached; event dropped");
			gatewayQueueLimitLogged_ = true;
		}
		return;
	}
	if (gatewayEvents_.size() < 2048) gatewayQueueLimitLogged_ = false;
	gatewayEvents_.push_back(payload);
}

void DiscordBot::setBotInfo(StringView id, StringView username)
{
	botId_ = std::string(id.data(), id.length());
	botUsername_ = std::string(username.data(), username.length());
}

void DiscordBot::completeInitialGuildSync()
{
	if (initialGuildSync_) initialGuildSync_ = false;
	if (!readyEventSent_)
	{
		readyEventSent_ = true;
		component_->onBotReadyEvent();
	}
}

void DiscordBot::handleGatewayMessage(const std::string& payload)
{
	const DiscordJson envelope = DiscordJson::parse(payload, nullptr, false);
	if (envelope.is_discarded() || !envelope.is_object()) return;
	const int op = jsonInt(envelope, "op", -1);
	if (op != 0 || !(envelope.find("t") != envelope.end()) || !envelope["t"].is_string()) return;
	const std::string event = envelope["t"].get<std::string>();
	const DiscordJson data = envelope.value("d", DiscordJson::object());
	if (!data.is_object() && !data.is_array()) return;
	if (event == "RATE_LIMITED")
	{
		if (jsonInt(data, "opcode", -1) == 8 && data.find("meta") != data.end() && data["meta"].is_object())
		{
			const std::string guildId = jsonString(data["meta"], "guild_id");
			if (!guildId.empty())
			{
				const double retryAfter = std::max(1.0, data.value("retry_after", 30.0));
				memberSyncRequested_.erase(guildId);
				memberSyncRetryAt_[guildId] = std::chrono::steady_clock::now() +
					std::chrono::milliseconds(static_cast<long long>(retryAfter * 1000.0) + 250);
			}
		}
		return;
	}

	if (event == "READY")
	{
		if ((data.find("user") != data.end()) && data["user"].is_object())
		{
			const std::string userJson = data["user"].dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
			component_->upsertUserFromJson(userJson);
			setBotInfo(DiscordUtils::extractJsonString(userJson, "id"), DiscordUtils::extractJsonString(userJson, "username"));
		}
		// The websocket thread may already be running, but the bot is not
		// considered connected until Discord has accepted IDENTIFY and sent
		// READY.  This keeps DBR_IsConnected and startup callbacks honest.
		connected_ = true;
		connecting_ = false;
		if ((data.find("private_channels") != data.end()) && data["private_channels"].is_array())
		{
			for (const auto& channel : data["private_channels"])
			{
				if (channel.is_object()) component_->upsertChannelFromJson(channel.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
			}
		}
		initialGuildIds_.clear();
		memberSyncRequested_.clear();
		memberSyncRetryAt_.clear();
		if ((data.find("guilds") != data.end()) && data["guilds"].is_array())
		{
			for (const auto& guild : data["guilds"])
			{
				if (guild.is_object() && guild.find("id") != guild.end() && guild["id"].is_string())
				{
					initialGuildIds_.insert(guild["id"].get<std::string>());
				}
			}
		}
		initialGuildSync_ = !initialGuildIds_.empty();
		if (!initialGuildSync_) completeInitialGuildSync();
		return;
	}
	if (event == "RESUMED")
	{
		connected_ = true;
		connecting_ = false;
		completeInitialGuildSync();
		return;
	}
	if (event == "INTERACTION_CREATE")
	{
		HandleDiscordInteractionPayload(data.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
		return;
	}

	if (event == "GUILD_CREATE" || event == "GUILD_UPDATE")
	{
		const std::string guildId = jsonString(data, "id");
		const bool wasCached = !guildId.empty() && component_->findGuildById(guildId) != nullptr;
		if (event == "GUILD_UPDATE" && !wasCached) return;
		const bool hasInlineMembers = data.find("members") != data.end() && data["members"].is_array();
		const bool useInlineMembers = hasInlineMembers && data["members"].size() <= 100;
		DiscordGuild* guild = component_->upsertGuildFromJson(data.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), useInlineMembers);
		if (guild)
		{
			if (event == "GUILD_CREATE")
			{
				const std::string guildId(guild->getGuildId().data(), guild->getGuildId().length());
				const bool wasInitialGuild = initialGuildSync_ && initialGuildIds_.erase(guildId) > 0;
				if (initialGuildSync_ && initialGuildIds_.empty()) completeInitialGuildSync();
				if (!wasInitialGuild && !wasCached) component_->onGuildCreateEvent(*guild);

				// Member lists are the expensive part of a guild snapshot.  Keep the
				// initial cache lightweight and ask the gateway for chunks separately;
				// chunks are then consumed incrementally on later ticks.
				if (websocket_ && guild->getMemberCount() > static_cast<int>(guild->getMemberIds().size()) &&
					memberSyncRequested_.insert(guildId).second)
				{
					websocket_->requestGuildMembers(guildId);
				}
			}
			else component_->onGuildUpdateEvent(*guild);
		}
		return;
	}
	if (event == "GUILD_DELETE")
	{
		const std::string guildId = jsonString(data, "id");
		if (jsonBool(data, "unavailable", false)) return;
		DiscordGuild* guild = static_cast<DiscordGuild*>(component_->findGuildById(guildId));
		if (!guild) return;
		component_->onGuildDeleteEvent(guildId);
		if (guild)
		{
			const std::vector<std::string> channelIds = guild->getChannelIds();
			const std::vector<std::string> roleIds = guild->getRoleIds();
			for (const std::string& channelId : channelIds) component_->removeChannel(channelId);
			for (const std::string& roleId : roleIds) component_->removeRole(roleId);
		}
		component_->removeGuild(guildId);
		memberSyncRequested_.erase(guildId);
		memberSyncRetryAt_.erase(guildId);
		return;
	}

	if (event == "CHANNEL_CREATE" || event == "CHANNEL_UPDATE" || event == "THREAD_CREATE" || event == "THREAD_UPDATE")
	{
		const std::string channelId = jsonString(data, "id");
		const bool wasCached = !channelId.empty() && component_->findChannelById(channelId) != nullptr;
		const bool isCreate = event == "CHANNEL_CREATE" || event == "THREAD_CREATE";
		if (!isCreate && !wasCached) return;
		DiscordChannel* channel = component_->upsertChannelFromJson(data.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
		if (channel)
		{
			if (isCreate && !wasCached) component_->onChannelCreateEvent(*channel);
			else if (!isCreate) component_->onChannelUpdateEvent(*channel);
		}
		return;
	}
	if (event == "CHANNEL_DELETE" || event == "THREAD_DELETE")
	{
		const std::string id = jsonString(data, "id");
		DiscordChannel* channel = static_cast<DiscordChannel*>(component_->findChannelById(id));
		if (channel) component_->onChannelDeleteEvent(*channel);
		component_->removeChannel(id);
		return;
	}

	if (event == "USER_UPDATE")
	{
		if (auto* user = component_->upsertUserFromJson(data.dump(-1, ' ', false, DiscordJson::error_handler_t::replace))) component_->onUserUpdateEvent(*user);
		return;
	}

	if (event == "GUILD_MEMBER_ADD" || event == "GUILD_MEMBER_UPDATE")
	{
		const std::string guildId = jsonString(data, "guild_id");
		DiscordGuild* guild = findOrCreateGuild(component_, guildId);
		DiscordUser* user = nullptr;
		if ((data.find("user") != data.end()) && data["user"].is_object())
		{
			const std::string memberUserId = jsonString(data["user"], "id");
			if (event == "GUILD_MEMBER_ADD") user = component_->upsertUserFromJson(data["user"].dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
			else if (!memberUserId.empty()) user = static_cast<DiscordUser*>(component_->findUserById(memberUserId));
		}
		const std::string userId = (data.find("user") != data.end()) && data["user"].is_object() ? jsonString(data["user"], "id") : std::string();
		if (guild && (event == "GUILD_MEMBER_ADD" || guild->findMember(userId))) guild->updateMemberFromJson(data.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), userId);
		if (guild && user)
		{
			if (event == "GUILD_MEMBER_UPDATE" && data["user"].is_object()) user->updateFromJson(data["user"].dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
			if (event == "GUILD_MEMBER_ADD") component_->onGuildMemberAddEvent(*guild, *user);
			else component_->onGuildMemberUpdateEvent(*guild, *user);
		}
		return;
	}
	if (event == "GUILD_MEMBER_REMOVE")
	{
		const std::string guildId = jsonString(data, "guild_id");
		std::string userId = jsonString(data, "user_id");
		if (userId.empty() && (data.find("user") != data.end()) && data["user"].is_object())
		{
			userId = jsonString(data["user"], "id");
		}
		DiscordGuild* guild = findOrCreateGuild(component_, guildId);
		DiscordUser* user = static_cast<DiscordUser*>(component_->findUserById(userId));
		if (guild && user) component_->onGuildMemberRemoveEvent(*guild, *user);
		if (guild) guild->removeMember(userId);
		return;
	}
	if (event == "VOICE_STATE_UPDATE")
	{
		const std::string guildId = jsonString(data, "guild_id");
		const std::string userId = jsonString(data, "user_id");
		DiscordGuild* guild = findOrCreateGuild(component_, guildId);
		DiscordUser* user = static_cast<DiscordUser*>(component_->findUserById(userId));
		if (guild && guild->findMember(userId)) guild->updateMemberFromJson(data.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), userId);
		DiscordChannel* channel = nullptr;
		const std::string channelId = jsonString(data, "channel_id");
		if (!channelId.empty()) channel = static_cast<DiscordChannel*>(component_->findChannelById(channelId));
		if (!channelId.empty() && !channel) return;
		if (guild && user) component_->onGuildMemberVoiceUpdateEvent(*guild, *user, channel);
		return;
	}
	if (event == "PRESENCE_UPDATE")
	{
		const std::string guildId = jsonString(data, "guild_id");
		const std::string userId = (data.find("user") != data.end()) && data["user"].is_object() ? jsonString(data["user"], "id") : jsonString(data, "user_id");
		DiscordGuild* guild = findOrCreateGuild(component_, guildId);
		DiscordUser* user = nullptr;
		if ((data.find("user") != data.end()) && data["user"].is_object())
		{
			const std::string presenceUserId = jsonString(data["user"], "id");
			if (!presenceUserId.empty()) user = static_cast<DiscordUser*>(component_->findUserById(presenceUserId));
		}
		if (!user && !userId.empty()) user = static_cast<DiscordUser*>(component_->findUserById(userId));
		DiscordJson member = { { "user_id", userId }, { "presence", { { "status", jsonString(data, "status", "offline") } } } };
		if (guild && guild->findMember(userId)) guild->updateMemberFromJson(member.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), userId);
		if (guild && user) component_->onGuildMemberUpdateEvent(*guild, *user);
		return;
	}

	if (event == "GUILD_ROLE_CREATE" || event == "GUILD_ROLE_UPDATE")
	{
		const std::string guildId = jsonString(data, "guild_id");
		DiscordGuild* guild = findOrCreateGuild(component_, guildId);
		const DiscordJson roleData = data.value("role", data);
		const std::string roleId = jsonString(roleData, "id");
		const bool wasCached = !roleId.empty() && component_->findRoleByIdInternal(roleId) != nullptr;
		if (event == "GUILD_ROLE_UPDATE" && !wasCached) return;
		DiscordRole* role = component_->upsertRoleFromJson(roleData.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), guildId);
		if (guild && role)
		{
			if (event == "GUILD_ROLE_CREATE" && !wasCached) component_->onGuildRoleCreateEvent(*guild, *role);
			else if (event == "GUILD_ROLE_UPDATE") component_->onGuildRoleUpdateEvent(*guild, *role);
		}
		return;
	}
	if (event == "GUILD_ROLE_DELETE")
	{
		const std::string guildId = jsonString(data, "guild_id");
		const std::string roleId = jsonString(data, "role_id");
		DiscordGuild* guild = findOrCreateGuild(component_, guildId);
		DiscordRole* role = static_cast<DiscordRole*>(component_->findRoleByIdInternal(roleId));
		if (guild && role) component_->onGuildRoleDeleteEvent(*guild, *role);
		component_->removeRole(roleId);
		return;
	}

	if (event == "MESSAGE_CREATE" || event == "MESSAGE_UPDATE")
	{
		if (auto* message = component_->upsertMessageFromJson(data.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)))
		{
			const std::string messageId(message->getMessageId().data(), message->getMessageId().length());
			if (event == "MESSAGE_CREATE")
			{
				component_->onMessageCreateEvent(*message);
				if (auto* current = static_cast<DiscordMessage*>(component_->findMessageById(messageId)); current && !current->isPersistent())
				{
					component_->removeMessage(messageId);
				}
			}
			else component_->onMessageUpdateEvent(*message);
		}
		return;
	}
	if (event == "MESSAGE_DELETE" || event == "MESSAGE_DELETE_BULK")
	{
		if (event == "MESSAGE_DELETE")
		{
			const std::string id = jsonString(data, "id");
			if (auto* message = static_cast<DiscordMessage*>(component_->findMessageById(id))) component_->onMessageDeleteEvent(*message);
			component_->removeMessage(id);
		}
		else if ((data.find("ids") != data.end()) && data["ids"].is_array())
		{
			for (const auto& item : data["ids"])
			{
				if (!item.is_string()) continue;
				const std::string id = item.get<std::string>();
				if (auto* message = static_cast<DiscordMessage*>(component_->findMessageById(id))) component_->onMessageDeleteEvent(*message);
				component_->removeMessage(id);
			}
		}
		return;
	}

	if (event == "MESSAGE_REACTION_ADD" || event == "MESSAGE_REACTION_REMOVE" || event == "MESSAGE_REACTION_REMOVE_ALL" || event == "MESSAGE_REACTION_REMOVE_EMOJI")
	{
		const std::string messageId = jsonString(data, "message_id");
		DiscordMessage* message = static_cast<DiscordMessage*>(component_->findMessageById(messageId));
		if (!message) return;
		DiscordUser* user = nullptr;
		const std::string userId = jsonString(data, "user_id");
		if (!userId.empty()) user = static_cast<DiscordUser*>(component_->findUserById(userId));
		if ((event == "MESSAGE_REACTION_ADD" || event == "MESSAGE_REACTION_REMOVE") && !user) return;
		cell emoji = 0;
		std::string emojiToken;
		if ((data.find("emoji") != data.end()) && data["emoji"].is_object())
		{
			const std::string name = jsonString(data["emoji"], "name");
			const std::string id = jsonString(data["emoji"], "id");
			if (!name.empty())
			{
				emojiToken = name;
				if (!id.empty()) emojiToken += ":" + id;
				emoji = CreateDiscordEmojiHandle(name, id);
			}
		}
		if (event != "MESSAGE_REACTION_REMOVE_ALL" && emoji == 0) return;
		const int type = event == "MESSAGE_REACTION_ADD" ? 0 : event == "MESSAGE_REACTION_REMOVE" ? 1 : event == "MESSAGE_REACTION_REMOVE_ALL" ? 2 : 3;
		component_->onMessageReactionEvent(*message, user, emoji, emojiToken, type);
		DeleteDiscordEmojiHandle(emoji);
		return;
	}

	if (event == "GUILD_MEMBERS_CHUNK" && (data.find("guild_id") != data.end()) && data["guild_id"].is_string())
	{
		DiscordGuild* guild = findOrCreateGuild(component_, data["guild_id"].get<std::string>());
		if (guild && (data.find("members") != data.end()) && data["members"].is_array())
		{
			constexpr size_t MAX_MEMBERS_PER_EVENT = 100;
			const size_t memberCount = data["members"].size();
			const size_t processed = std::min(MAX_MEMBERS_PER_EVENT, memberCount);
			for (size_t index = 0; index < processed; ++index)
			{
				const auto& member = data["members"][index];
				if (!member.is_object()) continue;
				guild->updateMemberFromJson(member.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
				if ((member.find("user") != member.end()) && member["user"].is_object()) component_->upsertUserFromJson(member["user"].dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
			}
			if (processed < memberCount)
			{
				DiscordJson remainder = DiscordJson::array();
				for (size_t index = processed; index < memberCount; ++index) remainder.push_back(data["members"][index]);
				DiscordJson nextData = data;
				nextData["members"] = std::move(remainder);
				enqueueGatewayMessage(DiscordJson({
					{ "op", 0 },
					{ "t", "GUILD_MEMBERS_CHUNK" },
					{ "d", std::move(nextData) }
				}).dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
			}
		}
	}
}
