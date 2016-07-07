#include "GameAnalytics.h"
#include "WebRequestHandler.h"
#include "SystemHelpers.h"

#include <json/json.h>

#if !IS_UWP_APP
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wldap32.lib")
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <assert.h>
#include <algorithm>

#ifdef min
#undef min
#endif

using namespace Analytics;

GameAnalytics::GameAnalytics(const std::string& secretKey, const std::string& gameId) :
	isInitialized(false),
	hasErrorHappened(false),
	shouldStopThread(false),
	restInitialized(false),
	sessionNumber(0),
	serverTimestamp(0),
	serverTimeDifference(0),
	sessionStartTimestamp(0),
	analyticsSendTimer(0),
	analyticsSendInterval(10.0f),
	httpRequestCounter(0),
	maxEventBatchSize(50),
	secretKey(secretKey),
	gameId(gameId)
{
}

GameAnalytics::~GameAnalytics()
{
	DeinitializeAndWaitForThread();
	if (!dbFileName.empty())
	{
		if (hasErrorHappened)
		{
			// Delete database file
			std::remove(dbFileName.c_str());
		}
	}
}

void GameAnalytics::Init(const InitData& initData)
{
	isInitialized = true;
	dbFileName = initData.databaseFileName;
	buildName = initData.buidName;
	hashedUserId = initData.userId;
	osVersion = SystemHelpers::GetOSVersion();;
	manufacturer = SystemHelpers::GetManufacturer();
	if (manufacturer.length() > 32)
		manufacturer = manufacturer.substr(0, std::min<int>(manufacturer.length(), 32));
	device = SystemHelpers::GetDevice();

	requestHandler.Initialize();

	threadHandle = std::thread(&GameAnalytics::ThreadedFunction, this);

	QueueFunctionToThread([this] {
		assert(!analyticsDatabase.IsInitialized()); // Already initialized!
		if (analyticsDatabase.IsInitialized())
			return;

		if (analyticsDatabase.Initialize(dbFileName.c_str()) != Result::Ok)
		{
			std::remove(dbFileName.c_str()); // Delete the db file and give it one more try
			if (analyticsDatabase.Initialize(dbFileName.c_str()) != Result::Ok)
			{
				assert(false);
				hasErrorHappened = true;
				return;
			}
		}

		sessionNumber = analyticsDatabase.GetNumSessions();
		sessionNumber++;
		analyticsDatabase.SetNumSessions(sessionNumber);

		Json::Value eventData;
		eventData["platform"] = "windows";
		eventData["os_version"] = osVersion;
		eventData["sdk_version"] = "rest api v2";

		Json::Value arrayData;
		arrayData.append(eventData);

		Json::FastWriter writer;
		std::string stringData = writer.write(arrayData);

		std::string hMacAuth;
		if (!SystemHelpers::GenerateHmac(stringData, secretKey, hMacAuth))
		{
			hasErrorHappened = true;
			return;
		}

		if (!GameAnalytics::SendToGameAnalytics("init", stringData, hMacAuth, 0))
		{
			assert(false);
			hasErrorHappened = true;
			return;
		}

		// This will prepare any previously cached events to be sent after initialization has been confirmed
		if (!analyticsDatabase.UnflagAllEvents())
		{
			assert(false);
			hasErrorHappened = true;
		}
		if (!EndUnendedSessions())
		{
			assert(false);
			hasErrorHappened = true;
		}
	});
}

void GameAnalytics::DeinitializeAndWaitForThread()
{
	if (isInitialized)
	{
		shouldStopThread = true;
		QueueFunctionToThread([] {}); // Queue an empty function to continue thread
		if (threadHandle.joinable())
			threadHandle.join();
		isInitialized = false;

		requestHandler.Deinitialize();
	}
}

bool GameAnalytics::IsInitialized() const
{
	return isInitialized && !hasErrorHappened;
}

void GameAnalytics::Update(float delta)
{
	assert(isInitialized);
	assert(std::this_thread::get_id() != threadHandle.get_id());

	if (requestHandler.IsInitialized())
	{
		QueueFunctionToThread([this, delta] {
			requestHandler.Update(delta);
		});
	}

	if (restInitialized)
	{
		analyticsSendTimer += delta;
		if (analyticsSendTimer >= analyticsSendInterval)
		{
			analyticsSendTimer = 0.0f;

			QueueFunctionToThread([this] {
				if (!SendCachedGameAnalyticsEvents())
				{
					OutputDebugStringA("SendCachedGameAnalyticsEvents() failed!\n");
					assert(false);
					hasErrorHappened = true;
					restInitialized = false;
				}
			});
		}
	}
}

void GameAnalytics::SendSessionStartEvent()
{
	assert(std::this_thread::get_id() != threadHandle.get_id());
	QueueFunctionToThread([this] {
		assert(sessionId.empty()); // Session is already active!
		sessionId = SystemHelpers::GenerateNewSessionID();

		Json::Value jsonValue;
		GenerateDefaultAnnotations(jsonValue);
		jsonValue["category"] = "user";

		sessionStartTimestamp = jsonValue.get("client_ts", 0).asInt64();

		if (!AddGameAnalyticsEvent(jsonValue))
		{
			assert(false);
			hasErrorHappened = true;
		}
	});
}

void GameAnalytics::SendSessionEndEvent()
{
	assert(std::this_thread::get_id() != threadHandle.get_id());
	QueueFunctionToThread([this] {
		assert(!sessionId.empty()); // No session active!

		Json::Value jsonValue;
		GenerateDefaultAnnotations(jsonValue);
		jsonValue["category"] = "session_end";
		jsonValue["length"] = jsonValue.get("client_ts", 0).asInt64() - sessionStartTimestamp;

		if (!AddGameAnalyticsEvent(jsonValue))
		{
			assert(false);
			hasErrorHappened = true;
		}

		sessionId.clear();
	});
}

void GameAnalytics::SendDesignEvent(const std::string& eventId, float value)
{
	assert(std::this_thread::get_id() != threadHandle.get_id());
	QueueFunctionToThread([this, eventId, value] {
		Json::Value jsonValue;
		GenerateDefaultAnnotations(jsonValue);
		jsonValue["category"] = "design";
		jsonValue["event_id"] = eventId;
		jsonValue["value"] = value;
		if (!AddGameAnalyticsEvent(jsonValue))
		{
			assert(false);
			hasErrorHappened = true;
		}
	});
}

void GameAnalytics::SendDesignEvent(const std::string& eventId)
{
	assert(std::this_thread::get_id() != threadHandle.get_id());
	QueueFunctionToThread([this, eventId] {
		Json::Value jsonValue;
		GenerateDefaultAnnotations(jsonValue);
		jsonValue["category"] = "design";
		jsonValue["event_id"] = eventId;
		if (!AddGameAnalyticsEvent(jsonValue))
		{
			assert(false);
			hasErrorHappened = true;
		}
	});
}

void GameAnalytics::SendProgressionEvent(ProgressionStatus::Enum status, const std::string& eventId)
{
	assert(std::this_thread::get_id() != threadHandle.get_id());

	QueueFunctionToThread([this, status, eventId] {
		assert(!sessionId.empty()); // No session active!
		assert(status != ProgressionStatus::Start || currentProgressionEventId.empty()); // Already in progress! #TODO: Fail progression and Start a new one

		currentProgressionEventId = eventId;

		Json::Value jsonValue;
		GenerateDefaultAnnotations(jsonValue);
		jsonValue["category"] = "progression";
		jsonValue["event_id"] = ProgressionStatus::ToString(status) + ":" + eventId;
		if (status == ProgressionStatus::Complete || status == ProgressionStatus::Fail)
		{
			int numAttempts = GetAndUpdateProgressionAttempts(status, eventId.c_str());
			jsonValue["attempt_num"] = numAttempts;
		}

		if (!AddGameAnalyticsEvent(jsonValue))
		{
			assert(false);
			hasErrorHappened = true;
		}

		if (status != ProgressionStatus::Start)
			currentProgressionEventId.clear();
	});
}

void GameAnalytics::SendProgressionEvent(ProgressionStatus::Enum status, const std::string& eventId, const int score)
{
	assert(std::this_thread::get_id() != threadHandle.get_id());

	QueueFunctionToThread([this, status, eventId, score] {
		assert(!sessionId.empty()); // No session active!
		assert(status != ProgressionStatus::Start || currentProgressionEventId.empty()); // Already in progress! #TODO: Fail progression and Start a new one

		currentProgressionEventId = eventId;

		Json::Value jsonValue;
		GenerateDefaultAnnotations(jsonValue);
		jsonValue["category"] = "progression";
		jsonValue["event_id"] = ProgressionStatus::ToString(status) + ":" + eventId;
		jsonValue["score"] = score;
		if (status == ProgressionStatus::Complete || status == ProgressionStatus::Fail)
		{
			int numAttempts = GetAndUpdateProgressionAttempts(status, eventId.c_str());
			jsonValue["attempt_num"] = numAttempts;
		}

		if (!AddGameAnalyticsEvent(jsonValue))
		{
			assert(false);
			hasErrorHappened = true;
		}

		if (status != ProgressionStatus::Start)
			currentProgressionEventId.clear();
	});
}

void GameAnalytics::ThreadedFunction()
{
	while (true)
	{
		std::queue<std::function<void()>> queueCopy;
		{
			std::unique_lock<std::mutex> lock(threadMutex);
			while (threadQueue.empty())
			{
				if (shouldStopThread)
					return; // Return here to make sure the queue is completely empty before stopping thread
				threadCondition.wait(lock);
			}
			std::swap(queueCopy, threadQueue);
		}
		while (!queueCopy.empty())
		{
			queueCopy.front()();
			queueCopy.pop();
		}
	}
}

void GameAnalytics::GenerateDefaultAnnotations(Json::Value& outAnnotations)
{
	assert(std::this_thread::get_id() == threadHandle.get_id());

	//assert(!sessionId.empty()); // No session has started yet!

	// Required
	outAnnotations["device"] = device;
	outAnnotations["v"] = 2;
	outAnnotations["user_id"] = hashedUserId;
	outAnnotations["client_ts"] = Timing::GetNowTime();
	outAnnotations["sdk_version"] = "rest api v2";
	outAnnotations["os_version"] = osVersion;
	assert(manufacturer.length() <= 32);
	outAnnotations["manufacturer"] = manufacturer;
	outAnnotations["platform"] = "windows";
	outAnnotations["session_id"] = sessionId;
	outAnnotations["session_num"] = sessionNumber;

	// Optional
	outAnnotations["build"] = buildName;

	if (!currentProgressionEventId.empty())
	{
		// This is not according to documentation, but can be found in another implementation..
		outAnnotations["progression"] = currentProgressionEventId;
	}
}

bool GameAnalytics::AddGameAnalyticsEvent(const Json::Value& eventData)
{
	assert(std::this_thread::get_id() == threadHandle.get_id());

	Json::FastWriter writer;
	std::string jsonString = writer.write(eventData);

	if (!analyticsDatabase.UpdateSessionEnds(eventData, jsonString.c_str(), sessionStartTimestamp))
	{
		assert(false);
		return false;
	}

	if (!analyticsDatabase.AddEvent(jsonString.c_str()))
	{
		assert(false);
		return false;
	}

	return true;
}

bool GameAnalytics::SendCachedGameAnalyticsEvents()
{
	assert(std::this_thread::get_id() == threadHandle.get_id());
	assert(restInitialized);

	// Flag the events as sent
	++httpRequestCounter;
	if (!analyticsDatabase.FlagEvents(httpRequestCounter, maxEventBatchSize))
	{
		OutputDebugStringA("FlagDatabaseEvents() failed!\n");
		return false;
	}

	Json::Value jsonValue;
	if (!analyticsDatabase.RetrieveFlaggedEvents(httpRequestCounter, jsonValue, serverTimeDifference))
	{
		OutputDebugStringA("RetrieveFlaggedDatabaseEvents() failed!\n");
		return false;
	}

	if (jsonValue.empty())
		return true; // Nothing to send, no error

	Json::FastWriter writer;
	std::string stringData = writer.write(jsonValue);

	std::string hMacAuth;
	if (!SystemHelpers::GenerateHmac(stringData, secretKey, hMacAuth))
		return false;

	int requestNum = httpRequestCounter;
	//QueueFunctionToMainThread([this, stringData, hMacAuth, requestNum] {
		if (!GameAnalytics::SendToGameAnalytics("events", stringData, hMacAuth, requestNum))
		{
			OutputDebugStringA("SendToGameAnalytics() failed!\n");
			hasErrorHappened = true;
		}
	//});

	return true;
}

int GameAnalytics::GetAndUpdateProgressionAttempts(ProgressionStatus::Enum status, const char* progressionEventId)
{
	assert(std::this_thread::get_id() == threadHandle.get_id());

	int numAttempts = 0;
	switch (status)
	{
	case ProgressionStatus::Start:
		break;

	case ProgressionStatus::Fail:
		if (!analyticsDatabase.GetProgressionAttempts(progressionEventId, numAttempts))
		{
			assert(false);
			hasErrorHappened = true;
		}
		numAttempts++;
		if (!analyticsDatabase.SetProgressionAttempts(progressionEventId, numAttempts))
		{
			assert(false);
			hasErrorHappened = true;
		}
		break;

	case ProgressionStatus::Complete:
		if (!analyticsDatabase.GetProgressionAttempts(progressionEventId, numAttempts))
		{
			assert(false);
			hasErrorHappened = true;
		}
		analyticsDatabase.DeleteProgressionAttempts(progressionEventId);
		numAttempts++;
		break;
	}
	return numAttempts;
}

bool GameAnalytics::EndUnendedSessions()
{
	assert(std::this_thread::get_id() == threadHandle.get_id());

	std::vector<GameAnalyticsDatabase::SessionEndData> toEndSessions;
	if (!analyticsDatabase.GetAllSessionEnds(toEndSessions))
		return false;

	for (auto itr = toEndSessions.begin(); itr != toEndSessions.end(); ++itr)
	{
		Json::Reader reader;
		Json::Value root;
		if (reader.parse(itr->annotations, root, false))
		{
			root["category"] = "session_end";
			root["length"] = root.get("client_ts", 0).asInt64() - itr->sessionStartTimestamp;

			if (!AddGameAnalyticsEvent(root))
				return false;
		}
	}

	return true;
}

bool GameAnalytics::SendToGameAnalytics(const std::string& route, const std::string& eventData, const std::string& hMacAuth, int requestId)
{
	using namespace std::placeholders;
	assert(std::this_thread::get_id() == threadHandle.get_id());

	const std::string absoluteUrl = "https://api.gameanalytics.com/v2/" + gameId + "/" + route;

#if IS_UWP_APP
	WebRequestHandler::RequestCompletedCallback callback = std::bind(&GameAnalytics::OnHTTPRequestCompletedThreadSafe, this, _1, _2, _3);
#else
	WebRequestHandler::RequestCompletedCallback callback = std::bind(&GameAnalytics::OnCurlHTTPRequestCompleted, this, _1, _2, _3);
#endif
	return requestHandler.SendHTTPRequest(absoluteUrl, eventData, hMacAuth, requestId, callback);
}

void GameAnalytics::QueueFunctionToThread(std::function<void()> func)
{
	assert(isInitialized);
	assert(std::this_thread::get_id() != threadHandle.get_id());
	{
		std::lock_guard<std::mutex> lock(threadMutex);
		threadQueue.push(func);
	}
	threadCondition.notify_one();
}

void GameAnalytics::OnHTTPRequestCompletedThreadSafe(const std::string& bodyData, int userData, int statusCode)
{
	std::string dataCopy = bodyData;
	QueueFunctionToThread([this, dataCopy, userData, statusCode]() {
		OnCurlHTTPRequestCompleted(dataCopy, userData, statusCode);
	});
}

void GameAnalytics::OnCurlHTTPRequestCompleted(const std::string& bodyData, int userData, int statusCode)
{
	OutputDebugStringA(bodyData.c_str());
	assert(std::this_thread::get_id() == threadHandle.get_id());
	OutputDebugStringA("HTTPRequestCompleted\n");

	if (!bodyData.empty())
	{
		Json::Value root;
		Json::Reader reader;
		if (reader.parse(bodyData, root, false))
		{
			if (!root.empty())
			{
				if (root.isArray())
				{
					for (auto itr = root.begin(); itr != root.end(); ++itr)
					{
						Json::Value& jsonValue = *itr;
						if (jsonValue.isObject() && !jsonValue.get("errors", Json::nullValue).isNull())
						{
							//assert(false); // Received an error! Should not assert in release
							int i = 123908;
						}
					}
				}

				if (root.isObject() && root.get("enabled", false).asBool())
				{
					serverTimestamp = root.get("server_ts", 0).asUInt();
					serverTimeDifference = serverTimestamp - Timing::GetNowTime();

					// Send any outstanding events next update
					analyticsSendTimer = analyticsSendInterval;

					restInitialized = true;
				}
			}
		}
	}

	if (userData != 0)
	{
		// http://apidocs.gameanalytics.com/REST.html#re-submitting-events
		switch (statusCode)
		{
		case 413:
			// Unflag database events so they will be sent again
			maxEventBatchSize /= 2; // Send less events next time
			if (maxEventBatchSize < 1) maxEventBatchSize = 1;
			analyticsDatabase.UnflagEvents(userData);
			break;

		case 0:
			// This status code is used when user is offline
			analyticsDatabase.UnflagEvents(userData);

			// Lost/no connection so disable event sending
			// #TODO: Try to check for internet connection again after a while
			restInitialized = false;
			break;

		default:
			analyticsDatabase.DeleteFlaggedEvents(userData);
			break;
		}
	}
}

std::string GameAnalytics::ProgressionStatus::ToString(ProgressionStatus::Enum value)
{
	switch (value)
	{
	case ProgressionStatus::Start:
		return "Start";
	case ProgressionStatus::Fail:
		return "Fail";
	case ProgressionStatus::Complete:
		return "Complete";
	}
	assert(false);
	return "";
}