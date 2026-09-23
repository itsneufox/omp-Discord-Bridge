/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

// REST lookups, entity details that are not part of the core getters, member
// moderation, the bot profile and message management.

#include "natives-internal.hpp"
#include "discord-bot.hpp"
#include "discord-channel.hpp"
#include "discord-component.hpp"
#include "discord-guild.hpp"
#include "discord-http.hpp"
#include "discord-json.hpp"
#include "discord-message.hpp"
#include "discord-role.hpp"
#include "discord-user.hpp"
#include "utils.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iterator>

namespace DiscordNatives
{
namespace
{
constexpr size_t MAX_IMAGE_BYTES = 10 * 1024 * 1024;
constexpr long long MAX_TIMEOUT_SECONDS = 28LL * 24 * 60 * 60;
constexpr long long MAX_BAN_DELETE_SECONDS = 7LL * 24 * 60 * 60;
constexpr uint64_t DISCORD_EPOCH_MS = 1420070400000ULL;
constexpr uint64_t BULK_DELETE_MAX_AGE_MS = 14ULL * 24 * 60 * 60 * 1000;

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

std::string toString(StringView value)
{
	return std::string(value.data(), value.length());
}

int64_t unixNow()
{
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string isoTimestamp(int64_t unixSeconds)
{
	const std::time_t time = static_cast<std::time_t>(unixSeconds);
	std::tm utc {};
#ifdef _WIN32
	gmtime_s(&utc, &time);
#else
	gmtime_r(&time, &utc);
#endif
	char buffer[32] {};
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
	return buffer;
}

int cdnSize(cell requested)
{
	for (int size = 16; size <= 4096; size *= 2)
	{
		if (size == requested) return size;
	}
	return 256;
}

std::string cdnUrl(const std::string& path, const std::string& hash, cell size)
{
	const bool animated = hash.rfind("a_", 0) == 0;
	return "https://cdn.discordapp.com/" + path + "/" + hash + (animated ? ".gif" : ".png") + "?size=" + std::to_string(cdnSize(size));
}

std::string userAvatarUrl(const DiscordUser& user, cell size)
{
	const std::string userId = toString(user.getUserId());
	if (!user.getAvatarHash().empty()) return cdnUrl("avatars/" + userId, user.getAvatarHash(), size);
	const uint64_t index = (DiscordUtils::stringToSnowflake(userId) >> 22) % 6;
	return "https://cdn.discordapp.com/embed/avatars/" + std::to_string(index) + ".png";
}

// Loads an image for the bot profile.  Accepts a ready data URI or a file
// path, which is also looked up inside scriptfiles/.
bool loadImageDataUri(const std::string& source, std::string& dataUri, std::string& error)
{
	if (source.rfind("data:image/", 0) == 0)
	{
		dataUri = source;
		return true;
	}
	std::string extension = source.substr(source.find_last_of('.') == std::string::npos ? source.size() : source.find_last_of('.') + 1);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	const char* mime = extension == "png" ? "image/png"
		: (extension == "jpg" || extension == "jpeg") ? "image/jpeg"
		: extension == "gif" ? "image/gif"
		: extension == "webp" ? "image/webp" : nullptr;
	if (!mime)
	{
		error = "unsupported image type (use png, jpg, gif or webp)";
		return false;
	}
	std::ifstream file(source, std::ios::binary);
	if (!file) file.open("scriptfiles/" + source, std::ios::binary);
	if (!file)
	{
		error = "image file '" + source + "' was not found";
		return false;
	}
	std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (bytes.empty() || bytes.size() > MAX_IMAGE_BYTES)
	{
		error = "image file must have between 1 byte and 10 MB";
		return false;
	}
	dataUri = std::string("data:") + mime + ";base64," + DiscordUtils::base64Encode(bytes);
	return true;
}

DiscordHTTP::Response localFailure(const std::string& message)
{
	return DiscordHTTP::Response { 0, DiscordJson { { "message", message } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), false, {}, 0.0, false };
}

DiscordGuild::Member* memberFor(cell guildHandle, cell userHandle)
{
	DiscordGuild* guild = resolveGuildByHandle(guildHandle);
	const std::string userId = userIdForHandle(userHandle);
	return guild && !userId.empty() ? guild->findMember(userId) : nullptr;
}

// ---------------------------------------------------------------------------
// Fetch natives
// ---------------------------------------------------------------------------

cell AMX_NATIVE_CALL Native_FetchGuild(AMX* amx, cell* params)
{
	const std::string guildId = hasParams(params, 3) ? guildIdForHandle(params[1]) : std::string();
	std::shared_ptr<PreparedPawnCallback> callback;
	if (guildId.empty() || !capturePawnCallback(amx, params[2], params[3], params, 4, callback)) return 0;
	return submitAction("DBR_FetchGuild", [guildId](DiscordHTTP& rest)
	{
		return rest.request(http::verb::get, "/guilds/" + guildId + "?with_counts=true");
	}, [callback](const DiscordHTTP::Response& response)
	{
		DiscordBridgeComponent* bridge = component();
		DiscordGuild* guild = response.success && bridge ? bridge->upsertGuildFromJson(response.body, false) : nullptr;
		if (callback) executePawnCallback(*callback, { guild ? assignGuildHandle(guild->getGuildId()) : 0 });
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_FetchChannel(AMX* amx, cell* params)
{
	const std::string channelId = hasParams(params, 3) ? channelIdForHandle(params[1]) : std::string();
	std::shared_ptr<PreparedPawnCallback> callback;
	if (channelId.empty() || !capturePawnCallback(amx, params[2], params[3], params, 4, callback)) return 0;
	return submitAction("DBR_FetchChannel", [channelId](DiscordHTTP& rest)
	{
		return rest.getChannel(channelId);
	}, [callback](const DiscordHTTP::Response& response)
	{
		DiscordBridgeComponent* bridge = component();
		DiscordChannel* channel = response.success && bridge ? bridge->upsertChannelFromJson(response.body) : nullptr;
		cell handle = 0;
		if (channel)
		{
			handle = assignChannelHandle(channel->getChannelId());
			rememberChannelGuild(handle, channel->getGuildId());
		}
		if (callback) executePawnCallback(*callback, { handle });
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_FetchRole(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string roleId = roleIdForHandle(params[2]);
	std::shared_ptr<PreparedPawnCallback> callback;
	if (guildId.empty() || roleId.empty() || !capturePawnCallback(amx, params[3], params[4], params, 5, callback)) return 0;
	return submitAction("DBR_FetchRole", [guildId, roleId](DiscordHTTP& rest)
	{
		return rest.request(http::verb::get, "/guilds/" + guildId + "/roles/" + roleId);
	}, [guildId, callback](const DiscordHTTP::Response& response)
	{
		DiscordBridgeComponent* bridge = component();
		DiscordRole* role = response.success && bridge ? bridge->upsertRoleFromJson(response.body, guildId) : nullptr;
		if (callback) executePawnCallback(*callback, { role ? assignRoleHandle(role->getRoleId()) : 0 });
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_FetchGuildMember(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string userId = userIdForHandle(params[2]);
	std::shared_ptr<PreparedPawnCallback> callback;
	if (guildId.empty() || userId.empty() || !capturePawnCallback(amx, params[3], params[4], params, 5, callback)) return 0;
	return submitAction("DBR_FetchGuildMember", [guildId, userId](DiscordHTTP& rest)
	{
		return rest.getGuildMember(guildId, userId);
	}, [guildId, userId, callback](const DiscordHTTP::Response& response)
	{
		DiscordBridgeComponent* bridge = component();
		cell userHandle = 0;
		const DiscordJson member = response.success ? DiscordJson::parse(response.body, nullptr, false) : DiscordJson();
		if (bridge && member.is_object())
		{
			if (auto user = member.find("user"); user != member.end() && user->is_object()) bridge->upsertUserFromJson(user->dump());
			if (DiscordGuild* guild = ensureGuildCached(guildId)) guild->updateMemberFromJson(response.body, userId);
			userHandle = assignUserHandle(userId);
		}
		if (callback) executePawnCallback(*callback, { assignGuildHandle(guildId), userHandle });
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_FetchUser(AMX* amx, cell* params)
{
	const std::string userId = hasParams(params, 3) ? userIdForHandle(params[1]) : std::string();
	std::shared_ptr<PreparedPawnCallback> callback;
	if (userId.empty() || !capturePawnCallback(amx, params[2], params[3], params, 4, callback)) return 0;
	return submitAction("DBR_FetchUser", [userId](DiscordHTTP& rest)
	{
		return rest.getUser(userId);
	}, [callback](const DiscordHTTP::Response& response)
	{
		DiscordBridgeComponent* bridge = component();
		DiscordUser* user = response.success && bridge ? bridge->upsertUserFromJson(response.body) : nullptr;
		if (callback) executePawnCallback(*callback, { user ? assignUserHandle(user->getUserId()) : 0 });
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_FetchMessage(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string channelId = channelIdForHandle(params[1]);
	const std::string messageId = getAmxString(amx, params[2]);
	std::shared_ptr<PreparedPawnCallback> callback;
	if (channelId.empty() || !isDiscordSnowflake(messageId) || !capturePawnCallback(amx, params[3], params[4], params, 5, callback)) return 0;
	return submitAction("DBR_FetchMessage", [channelId, messageId](DiscordHTTP& rest)
	{
		return rest.getMessage(channelId, messageId);
	}, [callback](const DiscordHTTP::Response& response)
	{
		completeMessageResponse(response.success, response.body, callback);
	}) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Entity details
// ---------------------------------------------------------------------------

cell AMX_NATIVE_CALL Native_GetGuildIcon(AMX* amx, cell* params)
{
	DiscordGuild* guild = hasParams(params, 3) ? resolveGuildByHandle(params[1]) : nullptr;
	return guild ? writeString(amx, params[2], params[3], guild->getIconHash()) : 0;
}

cell AMX_NATIVE_CALL Native_GetGuildIconUrl(AMX* amx, cell* params)
{
	DiscordGuild* guild = hasParams(params, 4) ? resolveGuildByHandle(params[1]) : nullptr;
	if (!guild || guild->getIconHash().empty()) return 0;
	return writeString(amx, params[2], params[3], cdnUrl("icons/" + toString(guild->getGuildId()), guild->getIconHash(), params[4]));
}

cell AMX_NATIVE_CALL Native_GetGuildBannerUrl(AMX* amx, cell* params)
{
	DiscordGuild* guild = hasParams(params, 4) ? resolveGuildByHandle(params[1]) : nullptr;
	if (!guild || guild->getBannerHash().empty()) return 0;
	return writeString(amx, params[2], params[3], cdnUrl("banners/" + toString(guild->getGuildId()), guild->getBannerHash(), params[4]));
}

cell AMX_NATIVE_CALL Native_GetGuildDescription(AMX* amx, cell* params)
{
	DiscordGuild* guild = hasParams(params, 3) ? resolveGuildByHandle(params[1]) : nullptr;
	return guild ? writeString(amx, params[2], params[3], guild->getDescription()) : 0;
}

cell AMX_NATIVE_CALL Native_GetGuildTotalMembers(AMX* amx, cell* params)
{
	DiscordGuild* guild = hasParams(params, 2) ? resolveGuildByHandle(params[1]) : nullptr;
	return guild && writeCell(amx, params[2], static_cast<cell>(guild->getMemberCount())) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetChannelSlowmode(AMX* amx, cell* params)
{
	DiscordChannel* channel = hasParams(params, 2) ? resolveChannelByHandle(params[1]) : nullptr;
	return channel && writeCell(amx, params[2], static_cast<cell>(channel->getRateLimitPerUser())) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetChannelSlowmode(AMX*, cell* params)
{
	const std::string channelId = hasParams(params, 2) ? channelIdForHandle(params[1]) : std::string();
	if (channelId.empty() || params[2] < 0 || params[2] > 21600) return 0;
	const std::string body = DiscordJson { { "rate_limit_per_user", params[2] } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
	return submitAction("DBR_SetChannelSlowmode", [channelId, body](DiscordHTTP& rest)
	{
		return rest.modifyChannel(channelId, body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_IsRoleManaged(AMX* amx, cell* params)
{
	DiscordRole* role = hasParams(params, 2) ? resolveRoleByHandle(params[1]) : nullptr;
	return role && writeCell(amx, params[2], role->isManaged() ? 1 : 0) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetUserGlobalName(AMX* amx, cell* params)
{
	DiscordUser* user = hasParams(params, 3) ? resolveUserByHandle(params[1]) : nullptr;
	return user ? writeString(amx, params[2], params[3], toString(user->getGlobalName())) : 0;
}

cell AMX_NATIVE_CALL Native_GetUserAvatarUrl(AMX* amx, cell* params)
{
	DiscordUser* user = hasParams(params, 4) ? resolveUserByHandle(params[1]) : nullptr;
	return user ? writeString(amx, params[2], params[3], userAvatarUrl(*user, params[4])) : 0;
}

cell AMX_NATIVE_CALL Native_GetUserBannerUrl(AMX* amx, cell* params)
{
	DiscordUser* user = hasParams(params, 4) ? resolveUserByHandle(params[1]) : nullptr;
	if (!user || user->getBannerHash().empty()) return 0;
	return writeString(amx, params[2], params[3], cdnUrl("banners/" + toString(user->getUserId()), user->getBannerHash(), params[4]));
}

cell AMX_NATIVE_CALL Native_IsUserSystem(AMX* amx, cell* params)
{
	DiscordUser* user = hasParams(params, 2) ? resolveUserByHandle(params[1]) : nullptr;
	return user && writeCell(amx, params[2], user->isSystem() ? 1 : 0) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberDisplayName(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	DiscordUser* user = resolveUserByHandle(params[2]);
	if (!user) return 0;
	const DiscordGuild::Member* member = memberFor(params[1], params[2]);
	std::string name = member ? member->nickname : std::string();
	if (name.empty()) name = toString(user->getGlobalName());
	if (name.empty()) name = toString(user->getUsername());
	return writeString(amx, params[3], params[4], name);
}

cell AMX_NATIVE_CALL Native_GetGuildMemberJoinedAt(AMX* amx, cell* params)
{
	const DiscordGuild::Member* member = hasParams(params, 4) ? memberFor(params[1], params[2]) : nullptr;
	return member ? writeString(amx, params[3], params[4], member->joinedAt) : 0;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberPremiumSince(AMX* amx, cell* params)
{
	const DiscordGuild::Member* member = hasParams(params, 4) ? memberFor(params[1], params[2]) : nullptr;
	return member ? writeString(amx, params[3], params[4], member->premiumSince) : 0;
}

cell AMX_NATIVE_CALL Native_GetGuildMemberAvatarUrl(AMX* amx, cell* params)
{
	if (!hasParams(params, 5)) return 0;
	DiscordUser* user = resolveUserByHandle(params[2]);
	if (!user) return 0;
	const DiscordGuild::Member* member = memberFor(params[1], params[2]);
	if (member && !member->avatarHash.empty())
	{
		const std::string path = "guilds/" + guildIdForHandle(params[1]) + "/users/" + toString(user->getUserId()) + "/avatars";
		return writeString(amx, params[3], params[4], cdnUrl(path, member->avatarHash, params[5]));
	}
	return writeString(amx, params[3], params[4], userAvatarUrl(*user, params[5]));
}

cell AMX_NATIVE_CALL Native_GetGuildMemberTimeout(AMX* amx, cell* params)
{
	const DiscordGuild::Member* member = hasParams(params, 3) ? memberFor(params[1], params[2]) : nullptr;
	if (!member) return 0;
	int64_t remaining = 0;
	if (!member->timeoutUntil.empty())
	{
		// Discord sends "2026-01-01T12:00:00.000000+00:00"; the parser expects UTC
		// with a trailing Z, and the timestamp is always UTC.
		const std::string utc = member->timeoutUntil.size() >= 19 ? member->timeoutUntil.substr(0, 19) + "Z" : std::string();
		remaining = static_cast<int64_t>(DiscordUtils::parseDiscordTimestamp(utc)) - unixNow();
		if (remaining < 0) remaining = 0;
	}
	return writeCell(amx, params[3], static_cast<cell>(remaining)) ? 1 : 0;
}

cell memberFlag(AMX* amx, cell* params, bool DiscordGuild::Member::*flag)
{
	const DiscordGuild::Member* member = hasParams(params, 3) ? memberFor(params[1], params[2]) : nullptr;
	return member && writeCell(amx, params[3], member->*flag ? 1 : 0) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_IsGuildMemberPending(AMX* amx, cell* params)
{
	return memberFlag(amx, params, &DiscordGuild::Member::pending);
}

cell AMX_NATIVE_CALL Native_IsGuildMemberMuted(AMX* amx, cell* params)
{
	return memberFlag(amx, params, &DiscordGuild::Member::mute);
}

cell AMX_NATIVE_CALL Native_IsGuildMemberDeafened(AMX* amx, cell* params)
{
	return memberFlag(amx, params, &DiscordGuild::Member::deaf);
}

// ---------------------------------------------------------------------------
// Member management
// ---------------------------------------------------------------------------

cell modifyMember(const char* action, cell guildHandle, cell userHandle, const DiscordJson& body, const std::string& reason)
{
	const std::string guildId = guildIdForHandle(guildHandle);
	const std::string userId = userIdForHandle(userHandle);
	if (guildId.empty() || userId.empty() || reason.size() > 512) return 0;
	return submitAction(action, [guildId, userId, payload = body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace), reason](DiscordHTTP& rest)
	{
		return rest.modifyGuildMember(guildId, userId, payload, reason);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_KickGuildMember(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string userId = userIdForHandle(params[2]);
	const std::string reason = getAmxString(amx, params[3]);
	if (guildId.empty() || userId.empty() || reason.size() > 512) return 0;
	return submitAction("DBR_KickGuildMember", [guildId, userId, reason](DiscordHTTP& rest)
	{
		return rest.removeGuildMember(guildId, userId, reason);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_BanGuildMember(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string userId = userIdForHandle(params[2]);
	const std::string reason = getAmxString(amx, params[3]);
	const cell deleteSeconds = params[4];
	if (guildId.empty() || userId.empty() || reason.size() > 512 || deleteSeconds < 0 || deleteSeconds > MAX_BAN_DELETE_SECONDS) return 0;
	return submitAction("DBR_BanGuildMember", [guildId, userId, reason, deleteSeconds](DiscordHTTP& rest)
	{
		return rest.createGuildMemberBan(guildId, userId, reason, static_cast<int>(deleteSeconds));
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_UnbanGuildMember(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string userId = userIdForHandle(params[2]);
	const std::string reason = getAmxString(amx, params[3]);
	if (guildId.empty() || userId.empty() || reason.size() > 512) return 0;
	return submitAction("DBR_UnbanGuildMember", [guildId, userId, reason](DiscordHTTP& rest)
	{
		return rest.removeGuildMemberBan(guildId, userId, reason);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetGuildMemberNickname(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string nickname = getAmxString(amx, params[3]);
	if (nickname.size() > 32) return 0;
	DiscordJson body = { { "nick", nickname.empty() ? DiscordJson(nullptr) : DiscordJson(nickname) } };
	return modifyMember("DBR_SetGuildMemberNickname", params[1], params[2], body, getAmxString(amx, params[4]));
}

cell AMX_NATIVE_CALL Native_SetGuildMemberTimeout(AMX* amx, cell* params)
{
	if (!hasParams(params, 4) || params[3] < 0 || params[3] > MAX_TIMEOUT_SECONDS) return 0;
	const DiscordJson until = params[3] == 0 ? DiscordJson(nullptr) : DiscordJson(isoTimestamp(unixNow() + params[3]));
	return modifyMember("DBR_SetGuildMemberTimeout", params[1], params[2], { { "communication_disabled_until", until } },
		getAmxString(amx, params[4]));
}

cell AMX_NATIVE_CALL Native_SetGuildMemberMute(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	return modifyMember("DBR_SetGuildMemberMute", params[1], params[2], { { "mute", params[3] != 0 } }, getAmxString(amx, params[4]));
}

cell AMX_NATIVE_CALL Native_SetGuildMemberDeaf(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	return modifyMember("DBR_SetGuildMemberDeaf", params[1], params[2], { { "deaf", params[3] != 0 } }, getAmxString(amx, params[4]));
}

cell AMX_NATIVE_CALL Native_SetGuildMemberVoiceChannel(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	DiscordJson channel = nullptr;
	if (params[3] != 0)
	{
		const std::string channelId = channelIdForHandle(params[3]);
		if (channelId.empty()) return 0;
		channel = channelId;
	}
	return modifyMember("DBR_SetGuildMemberVoiceChannel", params[1], params[2], { { "channel_id", channel } }, getAmxString(amx, params[4]));
}

cell changeMemberRole(AMX* amx, cell* params, bool add)
{
	if (!hasParams(params, 4)) return 0;
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string userId = userIdForHandle(params[2]);
	const std::string roleId = roleIdForHandle(params[3]);
	const std::string reason = getAmxString(amx, params[4]);
	if (guildId.empty() || userId.empty() || roleId.empty() || reason.size() > 512) return 0;
	return submitAction(add ? "DBR_AddGuildMemberRole" : "DBR_RemoveGuildMemberRole", [guildId, userId, roleId, reason, add](DiscordHTTP& rest)
	{
		return add ? rest.addGuildMemberRole(guildId, userId, roleId, reason) : rest.removeGuildMemberRole(guildId, userId, roleId, reason);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_AddGuildMemberRole(AMX* amx, cell* params)
{
	return changeMemberRole(amx, params, true);
}

cell AMX_NATIVE_CALL Native_RemoveGuildMemberRole(AMX* amx, cell* params)
{
	return changeMemberRole(amx, params, false);
}

// ---------------------------------------------------------------------------
// Bot profile
// ---------------------------------------------------------------------------

cell AMX_NATIVE_CALL Native_SetBotNickname(AMX* amx, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	const std::string guildId = guildIdForHandle(params[1]);
	const std::string nickname = getAmxString(amx, params[2]);
	if (guildId.empty() || nickname.size() > 32) return 0;
	const std::string body = DiscordJson { { "nick", nickname.empty() ? DiscordJson(nullptr) : DiscordJson(nickname) } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
	return submitAction("DBR_SetBotNickname", [guildId, body](DiscordHTTP& rest)
	{
		return rest.request(http::verb::patch, "/guilds/" + guildId + "/members/@me", body);
	}) ? 1 : 0;
}

void refreshBotUser(const DiscordHTTP::Response& response)
{
	DiscordBridgeComponent* bridge = component();
	if (!response.success || !bridge) return;
	if (DiscordUser* user = bridge->upsertUserFromJson(response.body))
	{
		if (DiscordBot* bot = nativeBot()) bot->setBotInfo(user->getUserId(), user->getUsername());
	}
}

cell AMX_NATIVE_CALL Native_SetBotUsername(AMX* amx, cell* params)
{
	const std::string username = hasParams(params, 1) ? getAmxString(amx, params[1]) : std::string();
	if (username.size() < 2 || username.size() > 32) return 0;
	const std::string body = DiscordJson { { "username", username } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
	return submitAction("DBR_SetBotUsername", [body](DiscordHTTP& rest)
	{
		return rest.request(http::verb::patch, "/users/@me", body);
	}, refreshBotUser) ? 1 : 0;
}

cell setBotImage(AMX* amx, cell* params, const char* field, const char* action)
{
	if (!hasParams(params, 1)) return 0;
	// A file path must keep the script's bytes: Windows opens it in the ANSI code page.
	const std::string source = getAmxStringRaw(amx, params[1]);
	const std::string key = field;
	// Reading and encoding the file happens on the REST worker so a large
	// image never stalls the server tick.
	return submitAction(action, [source, key](DiscordHTTP& rest)
	{
		DiscordJson body = { { key, nullptr } };
		if (!source.empty())
		{
			std::string dataUri;
			std::string error;
			if (!loadImageDataUri(source, dataUri, error)) return localFailure(error);
			body[key] = dataUri;
		}
		return rest.request(http::verb::patch, "/users/@me", body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
	}, refreshBotUser) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetBotAvatar(AMX* amx, cell* params)
{
	return setBotImage(amx, params, "avatar", "DBR_SetBotAvatar");
}

cell AMX_NATIVE_CALL Native_SetBotBanner(AMX* amx, cell* params)
{
	return setBotImage(amx, params, "banner", "DBR_SetBotBanner");
}

cell AMX_NATIVE_CALL Native_SetBotDescription(AMX* amx, cell* params)
{
	const std::string description = hasParams(params, 1) ? getAmxString(amx, params[1]) : std::string();
	if (description.size() > 400) return 0;
	const std::string body = DiscordJson { { "description", description } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
	return submitAction("DBR_SetBotDescription", [body](DiscordHTTP& rest)
	{
		return rest.request(http::verb::patch, "/applications/@me", body);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetBotPresenceStatus(AMX*, cell*)
{
	DiscordBot* bot = nativeBot();
	if (!bot) return 0;
	switch (bot->getPresenceStatus())
	{
		case 0: return 1;
		case 2: return 2;
		case 1: return 3;
		case 3: return 4;
		default: return 5;
	}
}

cell AMX_NATIVE_CALL Native_SetBotPresenceStatus(AMX*, cell* params)
{
	DiscordBot* bot = nativeBot();
	if (!bot || !hasParams(params, 1)) return 0;
	EDiscordPresenceStatus status;
	switch (params[1])
	{
		case 1: status = EDiscordPresenceStatus::Online; break;
		case 2: status = EDiscordPresenceStatus::Idle; break;
		case 3: status = EDiscordPresenceStatus::DoNotDisturb; break;
		case 4: status = EDiscordPresenceStatus::Invisible; break;
		case 5: status = EDiscordPresenceStatus::Offline; break;
		default: return 0;
	}
	return bot->setPresenceStatus(status) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_SetBotActivity(AMX* amx, cell* params)
{
	DiscordBot* bot = nativeBot();
	if (!bot || !hasParams(params, 3)) return 0;
	const std::string name = getAmxString(amx, params[1]);
	const std::string url = getAmxString(amx, params[3]);
	if (name.size() > 128 || url.size() > 512) return 0;
	return bot->setActivity(static_cast<int>(params[2]), name, url) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetBotUser(AMX*, cell*)
{
	DiscordBot* bot = nativeBot();
	if (!bot || bot->getBotId().empty()) return 0;
	return assignUserHandle(bot->getBotId());
}

// ---------------------------------------------------------------------------
// Message management
// ---------------------------------------------------------------------------

cell AMX_NATIVE_CALL Native_ReplyMessage(AMX* amx, cell* params)
{
	if (!hasParams(params, 5)) return 0;
	std::string channelId;
	std::string messageId;
	const std::string content = getAmxString(amx, params[2]);
	std::shared_ptr<PreparedPawnCallback> callback;
	if (!messageRefForHandle(params[1], channelId, messageId) || content.empty() || content.size() > 2000 ||
		!capturePawnCallback(amx, params[4], params[5], params, 6, callback)) return 0;
	DiscordJson body = {
		{ "content", content },
		{ "message_reference", { { "message_id", messageId }, { "fail_if_not_exists", false } } }
	};
	if (params[3] == 0) body["allowed_mentions"] = { { "parse", { "users", "roles", "everyone" } }, { "replied_user", false } };
	return submitAction("DBR_ReplyMessage", [channelId, payload = body.dump(-1, ' ', false, DiscordJson::error_handler_t::replace)](DiscordHTTP& rest)
	{
		return rest.sendMessagePayload(channelId, payload);
	}, [callback](const DiscordHTTP::Response& response)
	{
		completeMessageResponse(response.success, response.body, callback);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_DeleteMessageById(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const std::string channelId = channelIdForHandle(params[1]);
	const std::string messageId = getAmxString(amx, params[2]);
	const std::string reason = getAmxString(amx, params[3]);
	if (channelId.empty() || !isDiscordSnowflake(messageId) || reason.size() > 512) return 0;
	return submitAction("DBR_DeleteMessageByID", [channelId, messageId, reason](DiscordHTTP& rest)
	{
		return rest.request(http::verb::delete_, "/channels/" + channelId + "/messages/" + messageId, "", reason);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_BulkDeleteMessages(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const std::string channelId = channelIdForHandle(params[1]);
	const cell amount = params[2];
	std::shared_ptr<PreparedPawnCallback> callback;
	if (channelId.empty() || amount < 1 || amount > 100 || !capturePawnCallback(amx, params[3], params[4], params, 5, callback)) return 0;
	return submitAction("DBR_BulkDeleteMessages", [channelId, amount](DiscordHTTP& rest)
	{
		const DiscordHTTP::Response list = rest.request(http::verb::get, "/channels/" + channelId + "/messages?limit=" + std::to_string(amount));
		if (!list.success) return list;
		const DiscordJson messages = DiscordJson::parse(list.body, nullptr, false);
		std::vector<std::string> ids;
		const uint64_t nowMs = static_cast<uint64_t>(unixNow()) * 1000ULL;
		if (messages.is_array())
		{
			for (const auto& message : messages)
			{
				const std::string id = message.is_object() ? jsonString(message, "id") : std::string();
				const uint64_t createdMs = (DiscordUtils::stringToSnowflake(id) >> 22) + DISCORD_EPOCH_MS;
				// Discord refuses to bulk delete messages older than two weeks.
				if (!id.empty() && nowMs - createdMs < BULK_DELETE_MAX_AGE_MS) ids.push_back(id);
			}
		}
		DiscordHTTP::Response result { 200, {}, true, {}, 0.0, false };
		if (ids.size() == 1) result = rest.deleteMessage(channelId, ids.front());
		else if (ids.size() > 1) result = rest.request(http::verb::post, "/channels/" + channelId + "/messages/bulk-delete", DiscordJson { { "messages", ids } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace));
		if (result.success) result.body = DiscordJson { { "deleted", ids.size() } }.dump(-1, ' ', false, DiscordJson::error_handler_t::replace);
		return result;
	}, [callback](const DiscordHTTP::Response& response)
	{
		if (!callback) return;
		cell deleted = 0;
		const DiscordJson data = response.success ? DiscordJson::parse(response.body, nullptr, false) : DiscordJson();
		if (data.is_object()) deleted = static_cast<cell>(jsonInt(data, "deleted", 0));
		executePawnCallback(*callback, { deleted });
	}) ? 1 : 0;
}

cell messageAction(cell handle, const char* action, http::verb method, const std::string& suffixFormat)
{
	std::string channelId;
	std::string messageId;
	if (!messageRefForHandle(handle, channelId, messageId)) return 0;
	std::string endpoint = suffixFormat;
	const auto replace = [&endpoint](const std::string& token, const std::string& value)
	{
		const size_t position = endpoint.find(token);
		if (position != std::string::npos) endpoint.replace(position, token.size(), value);
	};
	replace("{channel}", channelId);
	replace("{message}", messageId);
	return submitAction(action, [method, endpoint](DiscordHTTP& rest)
	{
		return rest.request(method, endpoint);
	}) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_PinMessage(AMX*, cell* params)
{
	return messageAction(params[1], "DBR_PinMessage", http::verb::put, "/channels/{channel}/messages/pins/{message}");
}

cell AMX_NATIVE_CALL Native_UnpinMessage(AMX*, cell* params)
{
	return messageAction(params[1], "DBR_UnpinMessage", http::verb::delete_, "/channels/{channel}/messages/pins/{message}");
}

cell AMX_NATIVE_CALL Native_CrosspostMessage(AMX*, cell* params)
{
	return messageAction(params[1], "DBR_CrosspostMessage", http::verb::post, "/channels/{channel}/messages/{message}/crosspost");
}
}

void appendManagementNatives(std::vector<AMX_NATIVE_INFO>& natives)
{
	static const AMX_NATIVE_INFO kNatives[] = {
		{ "DBR_FetchGuild", Native_FetchGuild },
		{ "DBR_FetchChannel", Native_FetchChannel },
		{ "DBR_FetchRole", Native_FetchRole },
		{ "DBR_FetchGuildMember", Native_FetchGuildMember },
		{ "DBR_FetchUser", Native_FetchUser },
		{ "DBR_FetchMessage", Native_FetchMessage },

		{ "DBR_GetGuildIcon", Native_GetGuildIcon },
		{ "DBR_GetGuildIconURL", Native_GetGuildIconUrl },
		{ "DBR_GetGuildBannerURL", Native_GetGuildBannerUrl },
		{ "DBR_GetGuildDescription", Native_GetGuildDescription },
		{ "DBR_GetGuildTotalMembers", Native_GetGuildTotalMembers },
		{ "DBR_GetChannelSlowmode", Native_GetChannelSlowmode },
		{ "DBR_SetChannelSlowmode", Native_SetChannelSlowmode },
		{ "DBR_IsRoleManaged", Native_IsRoleManaged },
		{ "DBR_GetUserGlobalName", Native_GetUserGlobalName },
		{ "DBR_GetUserAvatarURL", Native_GetUserAvatarUrl },
		{ "DBR_GetUserBannerURL", Native_GetUserBannerUrl },
		{ "DBR_IsUserSystem", Native_IsUserSystem },
		{ "DBR_GetGuildMemberDisplayName", Native_GetGuildMemberDisplayName },
		{ "DBR_GetGuildMemberJoinedAt", Native_GetGuildMemberJoinedAt },
		{ "DBR_GetGuildMemberPremiumSince", Native_GetGuildMemberPremiumSince },
		{ "DBR_GetGuildMemberAvatarURL", Native_GetGuildMemberAvatarUrl },
		{ "DBR_GetGuildMemberTimeout", Native_GetGuildMemberTimeout },
		{ "DBR_IsGuildMemberPending", Native_IsGuildMemberPending },
		{ "DBR_IsGuildMemberMuted", Native_IsGuildMemberMuted },
		{ "DBR_IsGuildMemberDeafened", Native_IsGuildMemberDeafened },

		{ "DBR_KickGuildMember", Native_KickGuildMember },
		{ "DBR_BanGuildMember", Native_BanGuildMember },
		{ "DBR_UnbanGuildMember", Native_UnbanGuildMember },
		{ "DBR_SetGuildMemberNickname", Native_SetGuildMemberNickname },
		{ "DBR_SetGuildMemberTimeout", Native_SetGuildMemberTimeout },
		{ "DBR_SetGuildMemberMute", Native_SetGuildMemberMute },
		{ "DBR_SetGuildMemberDeaf", Native_SetGuildMemberDeaf },
		{ "DBR_SetGuildMemberVoiceChannel", Native_SetGuildMemberVoiceChannel },
		{ "DBR_AddGuildMemberRole", Native_AddGuildMemberRole },
		{ "DBR_RemoveGuildMemberRole", Native_RemoveGuildMemberRole },

		{ "DBR_SetBotNickname", Native_SetBotNickname },
		{ "DBR_SetBotUsername", Native_SetBotUsername },
		{ "DBR_SetBotAvatar", Native_SetBotAvatar },
		{ "DBR_SetBotBanner", Native_SetBotBanner },
		{ "DBR_SetBotDescription", Native_SetBotDescription },
		{ "DBR_GetBotPresenceStatus", Native_GetBotPresenceStatus },
		{ "DBR_SetBotPresenceStatus", Native_SetBotPresenceStatus },
		{ "DBR_SetBotActivity", Native_SetBotActivity },
		{ "DBR_GetBotUser", Native_GetBotUser },

		{ "DBR_ReplyMessage", Native_ReplyMessage },
		{ "DBR_DeleteMessageByID", Native_DeleteMessageById },
		{ "DBR_BulkDeleteMessages", Native_BulkDeleteMessages },
		{ "DBR_PinMessage", Native_PinMessage },
		{ "DBR_UnpinMessage", Native_UnpinMessage },
		{ "DBR_CrosspostMessage", Native_CrosspostMessage },
	};
	natives.insert(natives.end(), std::begin(kNatives), std::end(kNatives));
}
}
