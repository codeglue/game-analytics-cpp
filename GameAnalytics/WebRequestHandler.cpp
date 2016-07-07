#include "WebRequestHandler.h"

using namespace Analytics;

WebRequestHandler::WebRequestHandler()
{
}

WebRequestHandler::~WebRequestHandler()
{
}

void WebRequestHandler::Initialize()
{
	handler.Initialize();
}

void WebRequestHandler::Deinitialize()
{
	handler.Deinitialize();
}

bool WebRequestHandler::IsInitialized()
{
#if !IS_UWP_APP
	return handler.IsInitialized();
#else
	return false;
#endif
}

void WebRequestHandler::Update(float delta)
{
#if !IS_UWP_APP
	handler.Update(delta);
#endif
}

bool WebRequestHandler::SendHTTPRequest(const std::string& url, const std::string& postData, const std::string& authorizationData, int userData, RequestCompletedCallback callback)
{
	return handler.SendHTTPRequest(url, postData, authorizationData, userData, callback);
}