// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: MIT

#include <jni.h>
#include <android/log.h>

#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#define DTAG "EmuCoreVDiscord"
#define DLOGI(...) __android_log_print(ANDROID_LOG_INFO, DTAG, __VA_ARGS__)
#define DLOGW(...) __android_log_print(ANDROID_LOG_WARN, DTAG, __VA_ARGS__)

namespace
{
	enum class BridgeStatus : int
	{
		Disabled = 0,
		Disconnected = 1,
		Authorizing = 2,
		Connecting = 3,
		Connected = 4,
		Failed = 5,
	};

	struct BridgeState
	{
		std::mutex mutex;
		std::shared_ptr<discordpp::Client> client;
		std::atomic<int> status{static_cast<int>(BridgeStatus::Disconnected)};
		std::string fresh_token;
		std::string error;
		std::string self_name;
		std::string self_avatar;
		std::string details{"Browsing the game library"};
		std::string state;
		std::string cover_url;
	};

	BridgeState& S()
	{
		static BridgeState state;
		return state;
	}

	constexpr uint64_t kApplicationId = 1536775623287115786ull;
	constexpr const char* kRedirectUri = "discord-1536775623287115786:/authorize/callback";

	std::string JString(JNIEnv* env, jstring value)
	{
		if (!value)
			return {};
		const char* chars = env->GetStringUTFChars(value, nullptr);
		std::string result = chars ? chars : "";
		if (chars)
			env->ReleaseStringUTFChars(value, chars);
		return result;
	}

	std::string ProtocolField(std::string value)
	{
		for (char& character : value)
		{
			if (character == '\x1e' || character == '\x1f')
				character = ' ';
		}
		return value;
	}

	void SetStatus(BridgeStatus status)
	{
		S().status.store(static_cast<int>(status), std::memory_order_release);
	}

	void SetError(std::string error)
	{
		std::lock_guard<std::mutex> lock(S().mutex);
		S().error = std::move(error);
	}

	void RefreshSelf()
	{
		std::shared_ptr<discordpp::Client> client;
		{
			std::lock_guard<std::mutex> lock(S().mutex);
			client = S().client;
		}
		if (!client)
			return;

		const auto user = client->GetCurrentUserV2();
		if (!user.has_value())
			return;

		std::lock_guard<std::mutex> lock(S().mutex);
		S().self_name = user->DisplayName();
		S().self_avatar = user->AvatarUrl(
			discordpp::UserHandle::AvatarType::Webp,
			discordpp::UserHandle::AvatarType::Png);
	}

	void PushPresence()
	{
		std::shared_ptr<discordpp::Client> client;
		std::string details;
		std::string state;
		std::string cover;
		{
			std::lock_guard<std::mutex> lock(S().mutex);
			client = S().client;
			details = S().details;
			state = S().state;
			cover = S().cover_url;
		}
		if (!client)
			return;

		discordpp::Activity activity;
		activity.SetType(discordpp::ActivityTypes::Playing);
		activity.SetDetails(details.empty() ? std::string("Playing on EmuCoreV") : details);
		if (!state.empty())
			activity.SetState(state);

		if (!cover.empty())
		{
			discordpp::ActivityAssets assets;
			assets.SetLargeImage(cover);
			assets.SetLargeText(details.empty() ? std::string("EmuCoreV") : details);
			activity.SetAssets(std::move(assets));
		}

		client->UpdateRichPresence(std::move(activity), [](discordpp::ClientResult result) {
			if (!result.Successful())
				DLOGW("Rich Presence update failed: %s", result.Error().c_str());
		});
	}

	void WireCallbacks(const std::shared_ptr<discordpp::Client>& client)
	{
		client->SetStatusChangedCallback([](discordpp::Client::Status status,
			discordpp::Client::Error error, int32_t code) {
			if (status == discordpp::Client::Status::Ready)
			{
				SetStatus(BridgeStatus::Connected);
				RefreshSelf();
				PushPresence();
			}
			else if (status == discordpp::Client::Status::Disconnected)
			{
				SetStatus(BridgeStatus::Disconnected);
				if (error != discordpp::Client::Error::None)
					SetError(discordpp::Client::ErrorToString(error) + " (" + std::to_string(code) + ")");
			}
			else
			{
				SetStatus(BridgeStatus::Connecting);
			}
		});
	}

	void ConnectWithToken(const std::string& token)
	{
		std::shared_ptr<discordpp::Client> client;
		{
			std::lock_guard<std::mutex> lock(S().mutex);
			client = S().client;
		}
		if (!client || token.empty())
			return;

		SetStatus(BridgeStatus::Connecting);
		client->UpdateToken(discordpp::AuthorizationTokenType::Bearer, token,
			[client](discordpp::ClientResult result) {
				if (!result.Successful())
				{
					SetError(result.Error());
					SetStatus(BridgeStatus::Failed);
					return;
				}
				client->Connect();
			});
	}
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_available(JNIEnv*, jclass)
{
	return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_start(JNIEnv* env, jclass, jstring saved_token)
{
	const std::string token = JString(env, saved_token);
	{
		std::lock_guard<std::mutex> lock(S().mutex);
		if (!S().client)
		{
			S().client = std::make_shared<discordpp::Client>();
			S().client->SetApplicationId(kApplicationId);
			WireCallbacks(S().client);
			S().client->AddLogCallback([](std::string message, discordpp::LoggingSeverity severity) {
				if (severity <= discordpp::LoggingSeverity::Warning)
					DLOGW("[SDK] %s", message.c_str());
			}, discordpp::LoggingSeverity::Warning);
		}
		S().error.clear();
	}
	if (token.empty())
		SetStatus(BridgeStatus::Disconnected);
	else
		ConnectWithToken(token);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_authorize(JNIEnv*, jclass)
{
	std::shared_ptr<discordpp::Client> client;
	{
		std::lock_guard<std::mutex> lock(S().mutex);
		client = S().client;
		S().error.clear();
	}
	if (!client)
		return;

	client->AbortAuthorize();
	SetStatus(BridgeStatus::Authorizing);
	auto verifier = client->CreateAuthorizationCodeVerifier();

	discordpp::AuthorizationArgs args;
	args.SetClientId(kApplicationId);
	args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
	args.SetCodeChallenge(verifier.Challenge());

	client->Authorize(args, [client, verifier = verifier.Verifier()](
		discordpp::ClientResult result, std::string code, std::string) {
		if (!result.Successful() || code.empty())
		{
			SetError(result.Successful() ? "No authorization code returned" : result.Error());
			SetStatus(BridgeStatus::Failed);
			return;
		}

		client->GetToken(kApplicationId, code, verifier, kRedirectUri,
			[client](discordpp::ClientResult token_result, std::string token, std::string,
				discordpp::AuthorizationTokenType, int32_t, std::string) {
			if (!token_result.Successful() || token.empty())
			{
				SetError(token_result.Successful() ? "No access token returned" : token_result.Error());
				SetStatus(BridgeStatus::Failed);
				return;
			}
			{
				std::lock_guard<std::mutex> lock(S().mutex);
				S().fresh_token = token;
			}
			ConnectWithToken(token);
		});
	});
}

JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_takeToken(JNIEnv* env, jclass)
{
	std::string token;
	{
		std::lock_guard<std::mutex> lock(S().mutex);
		token.swap(S().fresh_token);
	}
	return token.empty() ? nullptr : env->NewStringUTF(token.c_str());
}

JNIEXPORT jint JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_status(JNIEnv*, jclass)
{
	return S().status.load(std::memory_order_acquire);
}

JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_error(JNIEnv* env, jclass)
{
	std::lock_guard<std::mutex> lock(S().mutex);
	return S().error.empty() ? nullptr : env->NewStringUTF(S().error.c_str());
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_setPresence(
	JNIEnv* env, jclass, jstring details, jstring state, jstring cover_url)
{
	{
		std::lock_guard<std::mutex> lock(S().mutex);
		S().details = JString(env, details);
		S().state = JString(env, state);
		S().cover_url = JString(env, cover_url);
	}
	if (S().status.load(std::memory_order_acquire) == static_cast<int>(BridgeStatus::Connected))
		PushPresence();
}

JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_self(JNIEnv* env, jclass)
{
	RefreshSelf();
	std::lock_guard<std::mutex> lock(S().mutex);
	std::string result = S().self_name;
	if (!result.empty())
	{
		result.push_back('\x1f');
		result += S().self_avatar;
	}
	return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_friends(JNIEnv* env, jclass)
{
	std::shared_ptr<discordpp::Client> client;
	{
		std::lock_guard<std::mutex> lock(S().mutex);
		client = S().client;
	}
	if (!client || S().status.load(std::memory_order_acquire) != static_cast<int>(BridgeStatus::Connected))
		return env->NewStringUTF("");

	std::string encoded;
	for (const auto& relationship :
		client->GetRelationshipsByGroup(discordpp::RelationshipGroupType::OnlinePlayingGame))
	{
		const auto user = relationship.User();
		if (!user.has_value())
			continue;

		const std::string name = ProtocolField(user->DisplayName());
		if (name.empty())
			continue;

		std::string activity_text;
		if (const auto activity = user->GameActivity(); activity.has_value())
			activity_text = ProtocolField(activity->Details().value_or(std::string()));
		const std::string avatar = ProtocolField(user->AvatarUrl(
			discordpp::UserHandle::AvatarType::Webp,
			discordpp::UserHandle::AvatarType::Png));

		if (!encoded.empty())
			encoded.push_back('\x1e');
		encoded += name;
		encoded.push_back('\x1f');
		encoded += activity_text;
		encoded.push_back('\x1f');
		encoded += avatar;
	}
	return env->NewStringUTF(encoded.c_str());
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_pump(JNIEnv*, jclass)
{
	discordpp::RunCallbacks();
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_discord_DiscordNative_stop(JNIEnv*, jclass)
{
	{
		std::lock_guard<std::mutex> lock(S().mutex);
		S().client.reset();
		S().fresh_token.clear();
		S().error.clear();
		S().self_name.clear();
		S().self_avatar.clear();
	}
	SetStatus(BridgeStatus::Disconnected);
}

}
