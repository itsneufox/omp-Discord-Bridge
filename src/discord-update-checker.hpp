/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class DiscordUpdateChecker
{
private:
	std::thread worker_;
	std::mutex resultMutex_;
	std::atomic<bool> completed_ { false };
	bool resultTaken_ = false;
	std::string latestVersion_;

public:
	~DiscordUpdateChecker();

	void start(const std::string& currentVersion);
	bool takeLatestVersion(std::string& latestVersion);
	void wait();
};
