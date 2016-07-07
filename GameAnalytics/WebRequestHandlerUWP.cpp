#include "WebRequestHandlerUWP.h"

#if IS_UWP_APP

#include "SystemHelpers.h"

#include <Windows.h>
#include <collection.h>
#include <ppltasks.h>
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Headers;

using namespace Analytics;

WebRequestHandlerUWP::WebRequestHandlerUWP() :
	httpClient(nullptr)
{

}

WebRequestHandlerUWP::~WebRequestHandlerUWP()
{

}

void WebRequestHandlerUWP::Initialize()
{
	httpClient = ref new Windows::Web::Http::HttpClient();
}

void WebRequestHandlerUWP::Deinitialize()
{
	delete httpClient;
	httpClient = nullptr;
}

bool WebRequestHandlerUWP::SendHTTPRequest(const std::string& url, const std::string& postData, const std::string& authorizationData, int userData, RequestCompletedCallback callback)
{
	// Build category URL.
	Platform::String^ absoluteUrlString = SystemHelpers::StringToPlatformString(url);

	// Send event to GameAnalytics.
	HttpRequestMessage^ message = ref new HttpRequestMessage();
	message->RequestUri = ref new Windows::Foundation::Uri(absoluteUrlString);
	message->Method = HttpMethod::Post;
	message->Content = ref new HttpStringContent(SystemHelpers::StringToPlatformString(postData),
		Windows::Storage::Streams::UnicodeEncoding::Utf8,
		ref new Platform::String(L"application/json"));
	message->Headers->TryAppendWithoutValidation(L"Authorization", SystemHelpers::StringToPlatformString(authorizationData));

	Concurrency::create_task(httpClient->SendRequestAsync(message)).then([=](HttpResponseMessage^ response)
	{
		std::string bodyContent = SystemHelpers::PlatformStringToString(response->Content->ToString());
		int statusCode = (int)response->StatusCode;
		callback(bodyContent, userData, statusCode);
	}).then([=](concurrency::task<void> t)
	{
		try
		{
			t.get();
		}
		catch (Platform::COMException^ e)
		{
			OutputDebugString(e->Message->Data());
		}
	});

	return true;
}

#endif