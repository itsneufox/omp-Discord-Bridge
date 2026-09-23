/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#pragma once

#include "discord-interface.hpp"
#include "discord-websocket.hpp"
#include "discord-http.hpp"
#include "discord-rest-queue.hpp"
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

class DiscordBridgeComponent;

class DiscordBot : public IDiscordBot
{
private:
	DiscordBridgeComponent* component_;
	ICore* core_;

	std::string botId_;
	std::string botUsername_;
	std::string botToken_;
	int intents_;

	std::atomic<bool> connected_;
	std::atomic<bool> connecting_;
	std::atomic<bool> shouldStop_;
	std::atomic<int> presenceStatus_ { 0 };
	// A presence update replaces the whole presence, so the current activity
	// is kept and sent again when only the status changes.
	int activityType_ = 0;
	std::string activityName_;
	std::string activityUrl_;

	std::unique_ptr<DiscordWebSocket> websocket_;
	std::unique_ptr<DiscordHTTP> http_;

	std::deque<std::string> gatewayEvents_;
	mutable std::mutex gatewayEventsMutex_;
	DiscordRestQueue restQueue_;
	std::thread restThread_;
	std::atomic<bool> restStop_ { false };
	std::deque<std::function<void()>> completionTasks_;
	std::mutex completionTasksMutex_;
	bool completionQueueLimitLogged_ = false;
	bool gatewayQueueLimitLogged_ = false;
	bool lastGatewayConnected_ = false;
	std::unordered_set<std::string> initialGuildIds_;
	std::unordered_set<std::string> memberSyncRequested_;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> memberSyncRetryAt_;
	bool initialGuildSync_ = false;
	bool readyEventSent_ = false;

	void enqueueGatewayMessage(const std::string& payload);
	void runRestTasks();
	void startRestWorker();
	void handleGatewayMessage(const std::string& payload);
	void completeInitialGuildSync();
	void serviceMemberSyncRetries();
	void initializeConnection();
	std::thread connectThread_;

public:
	DiscordBot(DiscordBridgeComponent* component, ICore* core, StringView token, int intents);
	~DiscordBot();

	StringView getBotId() const override;
	StringView getBotUsername() const override;
	bool isConnected() const override;
	bool isConnecting() const { return connecting_.load(); }
	// True once DBR_OnReady has been dispatched for this connection.
	bool isReady() const { return readyEventSent_; }

	bool setPresenceStatus(EDiscordPresenceStatus status) override;
	bool setActivity(EDiscordActivityType type, StringView name) override;
	bool setActivity(int type, StringView name, StringView url);
	bool disconnect() override;
	bool reconnect() override;

	bool connect();
	void stop();
	void update();
	bool sendChannelMessage(const std::string& channelId, const std::string& message,
		std::function<void(const DiscordHTTP::Response&)> completion = {});
	bool submitRestTask(std::function<void(DiscordHTTP&)> task);
	void enqueueCompletion(std::function<void()> task);

	DiscordHTTP* getHTTP() { return http_.get(); }
	DiscordWebSocket* getWebSocket() { return websocket_.get(); }

	void setBotInfo(StringView id, StringView username);
	int getPresenceStatus() const { return presenceStatus_.load(); }

	DiscordBridgeComponent* getComponent() const { return component_; }
};
