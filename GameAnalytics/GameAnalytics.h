#pragma once

#include <thread>
#include <queue>
#include <mutex>
#include <atomic>

#include "GameAnalyticsDatabase.h"
#include "WebRequestHandler.h"

namespace Json
{
	class Value;
};

namespace Analytics
{
	class GameAnalytics
	{
	public:
		struct ProgressionStatus
		{
			enum Enum
			{
				Start,
				Fail,
				Complete,
			};

			static std::string ToString(Enum value);
		};

		struct InitData
		{
			std::string databaseFileName;
			std::string buidName;
			std::string userId;
		};

		GameAnalytics(const std::string& secretKey, const std::string& gameId);
		~GameAnalytics();

		void Init(const InitData& initData);
		void DeinitializeAndWaitForThread();
		bool IsInitialized() const;

		void Update(float delta);

		void SendSessionStartEvent();
		void SendSessionEndEvent();

		void SendDesignEvent(const std::string& eventId, float value);
		void SendDesignEvent(const std::string& eventId);

		void SendProgressionEvent(ProgressionStatus::Enum status, const std::string& eventId);
		void SendProgressionEvent(ProgressionStatus::Enum status, const std::string& eventId, const int score);

	private:
		// This all runs in separate thread
		void ThreadedFunction();

		void GenerateDefaultAnnotations(Json::Value& outAnnotations);
		bool AddGameAnalyticsEvent(const Json::Value& eventData);
		bool SendCachedGameAnalyticsEvents();
		int GetAndUpdateProgressionAttempts(ProgressionStatus::Enum status, const char* progressionEventId);
		bool EndUnendedSessions();

	private:
		// This all runs in main thread
		bool SendToGameAnalytics(const std::string& route, const std::string& eventData, const std::string& hMacAuth, int requestId);
	public:
		void QueueFunctionToThread(std::function<void()> func);

	private:
		// These are shared between threads
		bool isInitialized;
		std::atomic<bool> hasErrorHappened;
		std::atomic<bool> shouldStopThread;
		std::thread threadHandle;
		std::queue< std::function<void()> > threadQueue;
		mutable std::mutex threadMutex;
		std::condition_variable threadCondition;
		std::atomic<bool> restInitialized;
		const std::string secretKey, gameId;

		// Can only access in thread
		int sessionNumber;
		unsigned int serverTimestamp;
		long long serverTimeDifference;

		long long sessionStartTimestamp;
		std::string sessionId;
		std::string hashedUserId;
		std::string osVersion;
		std::string manufacturer;
		std::string device;
		std::string buildName;
		std::string currentProgressionEventId;

		float analyticsSendTimer;
		const float analyticsSendInterval; // In seconds

		int httpRequestCounter;
		int maxEventBatchSize;

		std::string dbFileName;
		GameAnalyticsDatabase analyticsDatabase;
		WebRequestHandler requestHandler;

	public:
		void OnCurlHTTPRequestCompleted(const std::string& bodyData, int userData, int statusCode);
		void OnHTTPRequestCompletedThreadSafe(const std::string& bodyData, int userData, int statusCode);
	};
}