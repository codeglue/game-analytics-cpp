#pragma once

#if !defined(IS_UWP_APP) && defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
#define IS_UWP_APP 1
#endif

#include <string>
#include <list>
#include <functional>

#include "WebRequestHandlerUWP.h"
#include "WebRequestHandlerCurl.h"

namespace Analytics
{
	class WebRequestHandler
	{
		struct WebRequest;
	public:
		typedef std::function<void(const std::string&, int, int)> RequestCompletedCallback;

		WebRequestHandler();
		~WebRequestHandler();

		void Initialize();
		void Deinitialize();

		bool IsInitialized();

		void Update(float delta);

		bool SendHTTPRequest(const std::string& url, const std::string& postData, const std::string& authorizationData, int userData, RequestCompletedCallback callback);

	private:
#if IS_UWP_APP
		WebRequestHandlerUWP handler;
#else
		WebRequestHandlerCurl handler;
#endif
	};
}