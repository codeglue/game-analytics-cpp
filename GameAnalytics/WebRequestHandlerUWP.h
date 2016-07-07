#pragma once

#if !defined(IS_UWP_APP) && defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
#define IS_UWP_APP 1
#endif

#if IS_UWP_APP

#include <functional>

namespace Analytics
{
	class WebRequestHandlerUWP
	{
	public:
		typedef std::function<void(const std::string&, int, int)> RequestCompletedCallback;

		WebRequestHandlerUWP();
		~WebRequestHandlerUWP();

		void Initialize();
		void Deinitialize();

		bool SendHTTPRequest(const std::string& url, const std::string& postData, const std::string& authorizationData, int userData, RequestCompletedCallback callback);

	private:
		Windows::Web::Http::HttpClient^ httpClient;
	};
}

#endif