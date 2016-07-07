#pragma once

#include <string>

#if !defined(IS_UWP_APP) && defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
#define IS_UWP_APP 1
#endif

namespace Analytics
{
	class SystemHelpers
	{
	public:
		static std::string GenerateNewSessionID();
		static std::string HashUserId(const std::string& userId);

		static std::string GetOSVersion();
		static std::string GetManufacturer();
		static std::string GetDevice();

		static bool GenerateHmac(const std::string& json, const std::string& secretKey, std::string& outHmac);
		static std::string Base64Encode(const std::string& data);

#if IS_UWP_APP
		static Platform::String^ StringToPlatformString(const std::string& str);
		static std::string PlatformStringToString(Platform::String^ str);
#endif
	};

	class Timing
	{
	public:
		static long long Counter();
		static long long Frequency();
		static long long GetNowTime();
	};
}