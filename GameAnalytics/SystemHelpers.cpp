#include "SystemHelpers.h"

#include <vector>
#include <xlocbuf>
#include <codecvt>

//#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <assert.h>

#if IS_UWP_APP
#include <collection.h>
#include <ppltasks.h>
using namespace Windows::Security::Cryptography;
using namespace Windows::Security::Cryptography::Core;
using namespace Windows::Web::Http::Headers;

#else

// Crypto++
#include <osrng.h>
#include <hex.h>
#include <sha.h>
#include <hmac.h>
#include <base64.h>

// For generating UUID
#include <Rpc.h>
#pragma comment(lib, "Rpcrt4.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "Crypt32.lib")

#include <stdio.h>
#include <ole2.h>
#include <oleauto.h>
#include <wbemidl.h>
#include <comdef.h>

_COM_SMARTPTR_TYPEDEF(IWbemLocator, __uuidof(IWbemLocator));
_COM_SMARTPTR_TYPEDEF(IWbemServices, __uuidof(IWbemServices));
_COM_SMARTPTR_TYPEDEF(IWbemClassObject, __uuidof(IWbemClassObject));
_COM_SMARTPTR_TYPEDEF(IEnumWbemClassObject, __uuidof(IEnumWbemClassObject));
#endif

using namespace Analytics;

std::string SystemHelpers::GetOSVersion()
{
#if IS_UWP_APP
	auto deviceFamily = Windows::System::Profile::AnalyticsInfo::VersionInfo->DeviceFamily;
	return (deviceFamily == "Windows.Desktop") ? "windows 10" : "windows_phone 10";
#else
	std::string osVersion = "windows";

	std::string path;
	path.resize(MAX_PATH, '\0');
	static const char kernel32[] = "\\kernel32.dll";
	UINT n = GetSystemDirectory((char*)path.data(), MAX_PATH);
	if (n >= MAX_PATH || n == 0 || n > MAX_PATH - sizeof(kernel32) / sizeof(*kernel32))
		return "windows";

	memcpy((char*)path.data() + n, kernel32, sizeof(kernel32));

	DWORD versz = GetFileVersionInfoSize(path.c_str(), NULL);
	if (versz == 0)
		return osVersion;

	std::vector<char> ver(versz);
	if (!GetFileVersionInfo(path.c_str(), 0, versz, ver.data()))
		return osVersion;

	UINT blocksz;
	void* block;
	if (!VerQueryValue(ver.data(), "\\", &block, &blocksz) || blocksz < sizeof(VS_FIXEDFILEINFO))
		return osVersion;

	VS_FIXEDFILEINFO *vinfo = (VS_FIXEDFILEINFO *)block;

	std::string first = std::to_string((int)HIWORD(vinfo->dwProductVersionMS));
	std::string second = std::to_string((int)LOWORD(vinfo->dwProductVersionMS));
	std::string third = std::to_string((int)HIWORD(vinfo->dwProductVersionLS));

	return osVersion + " " + first + "." + second + "." + third;
#endif
}

#if !IS_UWP_APP
_bstr_t GetProperty(IWbemClassObject *pobj, PCWSTR pszProperty)
{
	_variant_t var;
	pobj->Get(pszProperty, 0, &var, nullptr, nullptr);
	return var;
}

PWSTR GetPropertyString(IWbemClassObject *pobj, PCWSTR pszProperty)
{
	return static_cast<PWSTR>(GetProperty(pobj, pszProperty));
}

void PrintProperty(IWbemClassObject *pobj, PCWSTR pszProperty)
{
	printf("%ls = %ls\n", pszProperty, GetPropertyString(pobj, pszProperty));
}
#endif

std::string Analytics::SystemHelpers::GetManufacturer()
{
#if IS_UWP_APP
	auto info = ref new Windows::Security::ExchangeActiveSyncProvisioning::EasClientDeviceInformation();
	return PlatformStringToString(info->SystemManufacturer);
#else
	// #TODO: Check errors
	// https://blogs.msdn.microsoft.com/oldnewthing/20140106-00/?p=2163
	return "Steam";

/*
	IWbemLocatorPtr spLocator;
	CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&spLocator));

	IWbemServicesPtr spServices;
	spLocator->ConnectServer(_bstr_t(L"root\\cimv2"), nullptr, nullptr, 0, 0, nullptr, nullptr, &spServices);

	CoSetProxyBlanket(spServices, RPC_C_AUTHN_DEFAULT,
		RPC_C_AUTHZ_DEFAULT, COLE_DEFAULT_PRINCIPAL,
		RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
		0, EOAC_NONE);

	IEnumWbemClassObjectPtr spEnum;
	spServices->ExecQuery(_bstr_t(L"WQL"),
		_bstr_t(L"select * from Win32_ComputerSystem"),
		WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
		nullptr, &spEnum);

	IWbemClassObjectPtr spObject;
	ULONG cActual;
	while (spEnum->Next(WBEM_INFINITE, 1, &spObject, &cActual) == WBEM_S_NO_ERROR) 
	{
// 		PrintProperty(spObject, L"Name");
// 		PrintProperty(spObject, L"Manufacturer");
// 		PrintProperty(spObject, L"Model");

		PWSTR manufacturerWs = GetPropertyString(spObject, L"Manufacturer");
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converterX;
		std::string manufacturer = converterX.to_bytes(manufacturerWs);

		// Trim whitespace
		size_t first = manufacturer.find_first_not_of(' ');
		size_t last = manufacturer.find_last_not_of(' ');
		if (first == std::string::npos || last == std::string::npos)
			return "unknown";

		return manufacturer.substr(first, (last - first + 1));
	}

	return 0;
	*/
#endif
}

std::string Analytics::SystemHelpers::GetDevice()
{
#if IS_UWP_APP
	auto info = ref new Windows::Security::ExchangeActiveSyncProvisioning::EasClientDeviceInformation();
	return PlatformStringToString(info->SystemProductName);
#else
	return "Steam device";
#endif
}

#if IS_UWP_APP
Platform::String^ SystemHelpers::StringToPlatformString(const std::string& str)
{
	std::wstring buffer;
	buffer.resize(MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, 0, 0));
	if (buffer.size() > 0)
	{
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &buffer[0], buffer.size());
		buffer.resize(buffer.size() - 1);
	}
	return ref new Platform::String(buffer.c_str());
}

std::string SystemHelpers::PlatformStringToString(Platform::String^ str)
{
	std::wstring s = str->Data();
	std::string buffer;
	buffer.resize(WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, 0, 0, NULL, NULL));
	if (buffer.size() > 0)
	{
		WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &buffer[0], buffer.size(), NULL, NULL);
		buffer.resize(buffer.size() - 1);
	}
	return buffer;
}
#endif

std::string SystemHelpers::GenerateNewSessionID()
{
#if IS_UWP_APP
	GUID result;
	if (SUCCEEDED(CoCreateGuid(&result)))
	{
		Platform::Guid guid(result);
		std::string guidString = PlatformStringToString(guid.ToString());
		return guidString.substr(1, guidString.length() - 2);
	}
	return "";
#else
	// https://msdn.microsoft.com/en-us/library/windows/desktop/aa379205(v=vs.85).aspx
	UUID Uuid;
	RPC_STATUS success = UuidCreate(&Uuid);
	assert(success == RPC_S_OK);

	RPC_CSTR stringUuid = NULL;
	success = UuidToString(&Uuid, &stringUuid);
	assert(success == RPC_S_OK);

	std::string id = (char*)stringUuid;

	success = RpcStringFree(&stringUuid);
	assert(success == RPC_S_OK);

	return id;
#endif
}

std::string SystemHelpers::HashUserId(const std::string& userId)
{
#if IS_UWP_APP
	assert(false);
	return userId;
#else
	// Hash the Id
	byte digest[CryptoPP::SHA::DIGESTSIZE];
	CryptoPP::SHA().CalculateDigest(digest, (byte*)userId.c_str(), userId.size());

	// Convert binary to a hex string
	std::string output;
	CryptoPP::HexEncoder encoder(new CryptoPP::StringSink(output));
	encoder.Put(digest, CryptoPP::SHA::DIGESTSIZE);
	encoder.MessageEnd();

	return output;
#endif
}

bool SystemHelpers::GenerateHmac(const std::string& json, const std::string& secretKey, std::string& outHmac)
{
#if IS_UWP_APP
	Platform::String^ jsonString = StringToPlatformString(json);
	Platform::String^ secretKeyString = StringToPlatformString(secretKey);

	auto alg = MacAlgorithmProvider::OpenAlgorithm(MacAlgorithmNames::HmacSha256);
	auto jsonBuffer = CryptographicBuffer::ConvertStringToBinary(jsonString, BinaryStringEncoding::Utf8);
	auto secretKeyBuffer = CryptographicBuffer::ConvertStringToBinary(secretKeyString, BinaryStringEncoding::Utf8);
	auto hmacKey = alg->CreateKey(secretKeyBuffer);

	auto hashedJsonBuffer = CryptographicEngine::Sign(hmacKey, jsonBuffer);
	auto hashedJsonBase64 = CryptographicBuffer::EncodeToBase64String(hashedJsonBuffer);
	outHmac = PlatformStringToString(hashedJsonBase64);

	return true;
#else
	// https://www.cryptopp.com/wiki/HMAC
	std::string mac;
	try
	{
		CryptoPP::HMAC< CryptoPP::SHA256 > hmac((byte*)secretKey.c_str(), secretKey.size());

		CryptoPP::StringSource ss2(json, true,
			new CryptoPP::HashFilter(hmac,
				new CryptoPP::StringSink(mac)
			) // HashFilter      
		); // StringSource
	}
	catch (...)
	{
		return false;
	}

	outHmac = Base64Encode(mac);
	return true;
#endif
}

std::string SystemHelpers::Base64Encode(const std::string& data)
{
#if IS_UWP_APP
	assert(false);
	return "";
#else
	std::string encoded;

	CryptoPP::Base64Encoder encoder(NULL, false);
	encoder.Put((byte*)data.data(), data.size());
	encoder.MessageEnd();

	CryptoPP::word64 size = encoder.MaxRetrievable();
	if (size)
	{
		encoded.resize((size_t)size);
		encoder.Get((byte*)encoded.data(), encoded.size());
	}

	return encoded;
#endif
}

long long Timing::Counter()
{
	LARGE_INTEGER li;
	if (!QueryPerformanceCounter(&li))
		assert(false);
	return li.QuadPart;
}

long long Timing::Frequency()
{
	LARGE_INTEGER li;
	if (!QueryPerformanceFrequency(&li))
		assert(false);
	return li.QuadPart;
}

long long Timing::GetNowTime()
{
	return Counter() / Frequency();
}