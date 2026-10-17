#include "discord-component.hpp"
#include "discord-channel.hpp"
#include "discord-guild.hpp"
#include <cassert>
#include <iostream>

logprintf_t logprintf = nullptr;

void test_guild_create_propagates_guild_id()
{
	auto* component = DiscordBridgeComponent::getInstance();

	const std::string guildId = "999999999999999999";
	const std::string channelId = "100000000000000001";

	component->storeGuild(std::make_unique<DiscordGuild>(nullptr, component, guildId, "Test Server"));
	component->storeChannel(std::make_unique<DiscordChannel>(nullptr, channelId, "general", EDiscordChannelType::GuildText));

	const std::string guildCreatePayload = R"({
		"id": "999999999999999999",
		"name": "Test Server",
		"channels": [
			{
				"id": "100000000000000001",
				"name": "general",
				"type": 0
			}
		]
	})";

	component->upsertGuildFromJson(guildCreatePayload);
	auto* channel = component->findChannelById(channelId);
	assert(channel != nullptr);

	assert(channel->getGuildId() == guildId && "REGRESSION: Channel failed to acquire guild_id from GUILD_CREATE!");

	std::cout << "[PASS] Verified: upsertGuildFromJson propagated parent guild_id to channel.\n";
}

void test_dm_channel_isolation()
{
	auto* component = DiscordBridgeComponent::getInstance();
	const std::string dmChannelId = "200000000000000002";

	component->storeChannel(std::make_unique<DiscordChannel>(nullptr, dmChannelId, "", EDiscordChannelType::DM));

	const std::string dmPayload = R"({
		"id": "200000000000000002",
		"type": 1
	})";

	component->upsertChannelFromJson(dmPayload);

	auto* dm = component->findChannelById(dmChannelId);
	assert(dm != nullptr);
	assert(dm->getGuildId().empty() && "REGRESSION: DM channel falsely acquired a guild_id");

	std::cout << "[PASS] Verified: DM channels remain without a guild_id.\n";
}

int main()
{
	test_guild_create_propagates_guild_id();
	test_dm_channel_isolation();

	std::cout << "\nComponent cache integration verified.\n";
	return 0;
}

