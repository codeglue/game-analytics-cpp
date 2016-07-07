#pragma once

#if !defined(IS_UWP_APP) && defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
#define IS_UWP_APP 1
#endif

#if !IS_UWP_APP

#include <functional>
#include <list>

namespace Analytics
{
	class WebRequestHandlerCurl
	{
		struct WebRequest;
	public:
		typedef std::function<void(const std::string&, int, int)> RequestCompletedCallback;

		WebRequestHandlerCurl();
		~WebRequestHandlerCurl();

		void Initialize();
		void Deinitialize();

		bool IsInitialized();

		void Update(float delta);

		bool SendHTTPRequest(const std::string& url, const std::string& postData, const std::string& authorizationData, int userData, RequestCompletedCallback callback);

	private:
		void OnRequestCompleted(WebRequest* request);

		static size_t writeDataCallback(void *ptr, size_t size, size_t nmemb, void* userData);

	private:
		std::list<WebRequest*> runningRequests;

	private:
		void* curlMultiHandle;
	};
}

#endif