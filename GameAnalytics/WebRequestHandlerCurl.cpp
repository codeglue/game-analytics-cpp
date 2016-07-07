#include "WebRequestHandlerCurl.h"

#if !IS_UWP_APP

#include <curl/curl.h>

using namespace Analytics;

struct WebRequestHandlerCurl::WebRequest
{
	int userData;
	std::string responseBody;
	CURL* curlHandle;
	std::string postData;
	curl_slist* headerList;
	RequestCompletedCallback callback;
};

WebRequestHandlerCurl::WebRequestHandlerCurl() :
	curlMultiHandle(nullptr)
{
}

WebRequestHandlerCurl::~WebRequestHandlerCurl()
{
}

void WebRequestHandlerCurl::Initialize()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curlMultiHandle = curl_multi_init();
}

void WebRequestHandlerCurl::Deinitialize()
{
	if (curlMultiHandle != NULL)
	{
		curl_multi_cleanup(curlMultiHandle);
		curlMultiHandle = nullptr;
		curl_global_cleanup();
	}
}

bool WebRequestHandlerCurl::IsInitialized()
{
	return (curlMultiHandle != nullptr);
}

void WebRequestHandlerCurl::Update(float delta)
{
	long curlTimeout = -1;
	curl_multi_timeout(curlMultiHandle, &curlTimeout);

	timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	if (curlTimeout >= 0)
	{
		timeout.tv_sec = curlTimeout / 1000;
		if (timeout.tv_sec > 1)
			timeout.tv_sec = 1;
		else
			timeout.tv_usec = (curlTimeout % 1000) * 1000;
	}

	int maxFileDescriptor = -1;
	fd_set fdRead = {}, fdWrite = {}, fdExcep = {};
	CURLMcode mc = curl_multi_fdset(curlMultiHandle, &fdRead, &fdWrite, &fdExcep, &maxFileDescriptor);
	if (mc != CURLM_OK)
	{
		OutputDebugStringA("curl_multi_fdset() failed\n");
		return;
	}

	int selectResult = 0;
	if (maxFileDescriptor != -1)
	{
		selectResult = select(maxFileDescriptor + 1, &fdRead, &fdWrite, &fdExcep, &timeout);
	}

	int stillRunning = 0;
	if (selectResult != -1)
	{
		curl_multi_perform(curlMultiHandle, &stillRunning);
	}
	else
	{
		OutputDebugStringA("select() failed\n");
	}

	CURLMsg* message = nullptr;
	int messagesLeft = 0;
	while ((message = curl_multi_info_read(curlMultiHandle, &messagesLeft)))
	{
		if (message->msg != CURLMSG_DONE)
			continue;

		WebRequest* request = nullptr;
		for (auto itr = runningRequests.begin(); itr != runningRequests.end(); ++itr)
		{
			if ((*itr)->curlHandle == message->easy_handle)
			{
				request = *itr;
				break;
			}
		}

		OutputDebugStringA("HTTP transfer completed\n");
		runningRequests.remove(request);

		OnRequestCompleted(request);
		curl_slist_free_all(request->headerList);
		delete request;
	}
}

bool WebRequestHandlerCurl::SendHTTPRequest(const std::string& url, const std::string& postData, const std::string& authorizationData, int userData, RequestCompletedCallback callback)
{
	WebRequest* request = new WebRequest();
	request->userData = userData;
	request->postData = postData;
	request->callback = callback;
	request->curlHandle = curl_easy_init();

	std::string authHeader = "Authorization:" + authorizationData;
	request->headerList = curl_slist_append(nullptr, authHeader.c_str());

	if (curl_easy_setopt(request->curlHandle, CURLOPT_URL, url.c_str()) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_URL) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_SSL_VERIFYPEER, 0L) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_SSL_VERIFYPEER) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_SSL_VERIFYHOST, 0L) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_SSL_VERIFYHOST) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_WRITEFUNCTION, writeDataCallback) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_WRITEFUNCTION) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_WRITEDATA, request) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_WRITEDATA) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_HTTPHEADER, request->headerList) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_HTTPHEADER) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_POST, 1) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_POST) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_POSTFIELDSIZE, request->postData.size()) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_POSTFIELDSIZE) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_POSTFIELDS, request->postData.c_str()) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_POSTFIELDS) failed\n");
		return false;
	}
#ifdef DEBUG
	if (curl_easy_setopt(request->curlHandle, CURLOPT_VERBOSE, 1L) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_VERBOSE) failed\n");
		return false;
	}
	if (curl_easy_setopt(request->curlHandle, CURLOPT_NOPROGRESS, 0L) != CURLE_OK)
	{
		OutputDebugStringA("curl_easy_setopt(CURLOPT_NOPROGRESS) failed\n");
		return false;
	}
#endif
	runningRequests.push_back(request);
	if (curl_multi_add_handle(curlMultiHandle, request->curlHandle) != CURLM_OK)
	{
		OutputDebugStringA("curl_multi_add_handle() failed\n");
		return false;
	}
	return true;

}

void WebRequestHandlerCurl::OnRequestCompleted(WebRequest* request)
{
	long http_code = 0;
	curl_easy_getinfo(request->curlHandle, CURLINFO_RESPONSE_CODE, &http_code);
	request->callback(request->responseBody, request->userData, http_code);
}

size_t WebRequestHandlerCurl::writeDataCallback(void *ptr, size_t size, size_t nmemb, void* userData)
{
	WebRequest* request = static_cast<WebRequest*>(userData);
	request->responseBody.append((const char*)ptr, size*nmemb);
	return size*nmemb;
}

#endif