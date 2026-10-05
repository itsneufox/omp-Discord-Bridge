/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#include "samp-plugin.hpp"
#include "samp-amx.hpp"
#include "samp-config.hpp"
#include "discord-component.hpp"
#include "discord-log.hpp"
#include "version.hpp"
#include <cstdlib>

logprintf_t logprintf = nullptr;

namespace
{
std::string environmentValue(const char* name)
{
	const char* value = std::getenv(name);
	return value ? std::string(value) : std::string();
}

std::string firstValue(const char* environmentName, const char* configName, const char* fallbackConfigName = nullptr)
{
	std::string value = environmentValue(environmentName);
	if (!value.empty())
	{
		return value;
	}

	value = readSampConfigValue(configName);
	if (value.empty() && fallbackConfigName)
	{
		value = readSampConfigValue(fallbackConfigName);
	}
	return value;
}

int configuredIntents()
{
	const std::string value = firstValue("DISCORD_BOT_INTENTS", "discord_bot_intents", "discord.intents");
	if (value.empty())
	{
		return DISCORD_DEFAULT_INTENTS;
	}
	try
	{
		return std::stoi(value);
	}
	catch (...)
	{
		return DISCORD_DEFAULT_INTENTS;
	}
}

bool configuredUpdateCheck()
{
	const std::string value = firstValue(
		"DISCORD_CHECK_FOR_UPDATES", "discord_check_for_updates", "discord.check_for_updates");
	return value.empty() || DiscordMessageBatchConfig::enabled(value);
}
}

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports()
{
	return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES | SUPPORTS_PROCESS_TICK;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void** data)
{
	if (!data)
	{
		return false;
	}

	SampAmx::setFunctionTable(data[PLUGIN_DATA_AMX_EXPORTS]);
	logprintf = reinterpret_cast<logprintf_t>(data[PLUGIN_DATA_LOGPRINTF]);
	DiscordLogSetMainThread();

	const std::string token = firstValue("DISCORD_BOT_TOKEN", "discord_bot_token", "discord.bot_token");
	const std::string channelId = firstValue("DISCORD_CHANNEL_ID", "discord_channel_id", "discord.channel_id");
	const std::string channelName = firstValue("DISCORD_CHANNEL_NAME", "discord_channel_name", "discord.channel_name");
	const int batchInterval = DiscordMessageBatchConfig::interval(
		firstValue("DISCORD_BATCH_INTERVAL_MS", "discord_batch_interval_ms", "discord.batch_interval_ms"));
	const bool batchRateLimited = DiscordMessageBatchConfig::enabled(
		firstValue("DISCORD_BATCH_RATE_LIMITED", "discord_batch_rate_limited", "discord.batch_rate_limited"));
	DiscordBridgeComponent::getInstance()->start(token, configuredIntents(), channelId, channelName,
		batchInterval, batchRateLimited, configuredUpdateCheck());

	if (logprintf)
	{
		logprintf("[DiscordBridge] discord-bridge v%s loaded as SA-MP plugin", DISCORD_BRIDGE_VERSION);
		if (token.empty())
		{
			logprintf("[DiscordBridge] No token in DISCORD_BOT_TOKEN or server.cfg; call DBR_ConnectBot in the gamemode to connect");
		}
	}
	return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload()
{
	DiscordBridgeComponent* bridge = DiscordBridgeComponent::getInstance();
	bridge->free();
	SampAmx::setFunctionTable(nullptr);
	if (logprintf)
	{
		logprintf("[DiscordBridge] SA-MP plugin unloaded");
	}
	logprintf = nullptr;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX* amx)
{
	DiscordBridgeComponent::getInstance()->onAmxLoad(amx);
	return AMX_ERR_NONE;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX* amx)
{
	DiscordBridgeComponent::getInstance()->onAmxUnload(amx);
	return AMX_ERR_NONE;
}

PLUGIN_EXPORT void PLUGIN_CALL ProcessTick()
{
	DiscordBridgeComponent::getInstance()->onTick(Microseconds {}, TimePoint {});
}
