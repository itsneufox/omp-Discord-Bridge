/*
 *  Discord Bridge for open.mp
 *  Copyright (c) 2026 Neufox
 */

#include "discord-update-checker.hpp"
#include "discord-json.hpp"
#include "discord-tls.hpp"
#include "version.hpp"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <string_view>

namespace
{
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

constexpr const char* GITHUB_HOST = "api.github.com";
constexpr const char* RELEASE_PATH = "/repos/itsneufox/omp-Discord-Bridge/releases/latest";
constexpr auto UPDATE_CHECK_TIMEOUT = std::chrono::seconds(5);

bool parseVersion(std::string_view version, std::array<std::uint64_t, 3>& parts)
{
	if (!version.empty() && version.front() == 'v') version.remove_prefix(1);
	std::size_t start = 0;
	for (std::size_t index = 0; index < parts.size(); ++index)
	{
		const std::size_t separator = version.find('.', start);
		if ((index < parts.size() - 1 && separator == std::string_view::npos) ||
			(index == parts.size() - 1 && separator != std::string_view::npos))
		{
			return false;
		}
		const std::size_t end = separator == std::string_view::npos ? version.size() : separator;
		if (end == start) return false;
		for (std::size_t character = start; character < end; ++character)
		{
			if (version[character] < '0' || version[character] > '9') return false;
		}
		const auto parsed = std::from_chars(version.data() + start, version.data() + end, parts[index]);
		if (parsed.ec != std::errc() || parsed.ptr != version.data() + end) return false;
		start = end + 1;
	}
	return start == version.size() + 1;
}

std::string requestLatestRelease()
{
	net::io_context io;
	ssl::context context { ssl::context::tlsv12_client };
	DiscordTLS::configureCertificateVerification(context);
	context.set_verify_mode(ssl::verify_peer);
	context.set_verify_callback(ssl::host_name_verification(GITHUB_HOST));
	tcp::resolver resolver { io };
	beast::ssl_stream<beast::tcp_stream> stream { io, context };
	if (!SSL_set_tlsext_host_name(stream.native_handle(), GITHUB_HOST)) return {};
	beast::flat_buffer buffer;
	net::steady_timer timer { io };
	beast::error_code failure;
	bool timedOut = false;
	http::response<http::string_body> response;
	http::request<http::empty_body> request { http::verb::get, RELEASE_PATH, 11 };
	request.set(http::field::host, GITHUB_HOST);
	request.set(http::field::user_agent, std::string("discord-bridge/") + DISCORD_BRIDGE_VERSION);
	request.set(http::field::accept, "application/vnd.github+json");
	request.keep_alive(false);

	auto finish = [&](beast::error_code error)
	{
		if (error) failure = error;
		timer.cancel();
	};
	auto sendRequest = [&]()
	{
		http::async_write(stream, request, [&](beast::error_code error, size_t)
		{
			if (error)
			{
				finish(error);
				return;
			}
			http::async_read(stream, buffer, response, [&](beast::error_code readError, size_t)
			{
				finish(readError);
			});
		});
	};

	timer.expires_after(UPDATE_CHECK_TIMEOUT);
	timer.async_wait([&](beast::error_code error)
	{
		if (error) return;
		timedOut = true;
		resolver.cancel();
		beast::error_code ignored;
		beast::get_lowest_layer(stream).socket().close(ignored);
	});
	resolver.async_resolve(GITHUB_HOST, "443", [&](beast::error_code error, tcp::resolver::results_type results)
	{
		if (error || timedOut)
		{
			finish(error);
			return;
		}
		beast::get_lowest_layer(stream).async_connect(results, [&](beast::error_code connectError, const tcp::endpoint&)
		{
			if (connectError)
			{
				finish(connectError);
				return;
			}
			stream.async_handshake(ssl::stream_base::client, [&](beast::error_code handshakeError)
			{
				if (handshakeError)
				{
					finish(handshakeError);
					return;
				}
				sendRequest();
			});
		});
	});
	io.run();
	if (timedOut || failure || response.result() != http::status::ok) return {};
	return response.body();
}

std::string findNewerRelease(const std::string& currentVersion)
{
	try
	{
		const std::string body = requestLatestRelease();
		if (body.empty()) return {};
		const DiscordJson release = DiscordJson::parse(body, nullptr, false);
		if (!release.is_object() || jsonBool(release, "prerelease", true) || jsonBool(release, "draft", true)) return {};
		const std::string tag = jsonString(release, "tag_name");
		std::array<std::uint64_t, 3> currentParts {};
		std::array<std::uint64_t, 3> latestParts {};
		if (!parseVersion(currentVersion, currentParts) || !parseVersion(tag, latestParts) || latestParts <= currentParts)
		{
			return {};
		}
		return tag;
	}
	catch (...)
	{
		// Update checks are best-effort and must never affect plugin startup.
		return {};
	}
}
}

DiscordUpdateChecker::~DiscordUpdateChecker()
{
	wait();
}

void DiscordUpdateChecker::start(const std::string& currentVersion)
{
	if (worker_.joinable()) return;
	completed_ = false;
	worker_ = std::thread([this, currentVersion]()
	{
		std::string latest;
		try
		{
			latest = findNewerRelease(currentVersion);
		}
		catch (...)
		{
			// A failed update check is intentionally silent.
		}
		{
			std::lock_guard<std::mutex> lock(resultMutex_);
			latestVersion_.swap(latest);
		}
		completed_.store(true, std::memory_order_release);
	});
}

bool DiscordUpdateChecker::takeLatestVersion(std::string& latestVersion)
{
	if (!completed_.load(std::memory_order_acquire)) return false;
	wait();
	if (resultTaken_) return false;
	resultTaken_ = true;
	std::lock_guard<std::mutex> lock(resultMutex_);
	latestVersion = latestVersion_;
	return true;
}

void DiscordUpdateChecker::wait()
{
	if (worker_.joinable()) worker_.join();
}
