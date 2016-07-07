#pragma once

#include <string>
#include <vector>

#include "GameAnalyticsResult.h"

namespace Json
{
	class Value;
};

struct sqlite3;

namespace Analytics
{
	class GameAnalyticsDatabase
	{
	public:
		struct SessionEndData
		{
			long long sessionStartTimestamp;
			std::string sessionId;
			std::string annotations;
		};

		GameAnalyticsDatabase();
		~GameAnalyticsDatabase();

		Result::Enum Initialize(const char* databaseFile);
		bool IsInitialized() const;

	public:
		int GetNumSessions() const;
		void SetNumSessions(int sessions);
		bool SetKeyValuePair(const char* key, int value);
		bool GetKeyValuePair(const char* key, int& outValue) const;

		bool AddEvent(const char* eventData);

		bool FlagEvents(int requestId, int amount);
		bool UnflagEvents(int requestId);
		bool UnflagAllEvents();
		bool RetrieveFlaggedEvents(int requestId, Json::Value& outJson, long long serverTimeDifference) const;
		bool DeleteFlaggedEvents(int requestId);

		bool GetProgressionAttempts(const char* progressionEventId, int& outAttempts);
		bool SetProgressionAttempts(const char* progressionEventId, int attempts);
		bool DeleteProgressionAttempts(const char* progressionEventId);

		bool UpdateSessionEnds(const Json::Value& eventData, const char* jsonString, long long sessionStartTimestamp);
		bool GetAllSessionEnds(std::vector<SessionEndData>& outSessionEndData) const;

	private:
		struct ColumnDescription
		{
			ColumnDescription(const char* name, const char* type, bool notNull = false, bool primaryKey = false, bool hasDefaultValue = false, int defaultValue = 0)
				: name(name), type(type), notNull(notNull), primaryKey(primaryKey), hasDefaultValue(hasDefaultValue), defaultValue(defaultValue)
			{}

			const char* name;
			const char* type;
			bool notNull;
			bool primaryKey;
			bool hasDefaultValue;
			int defaultValue;
		};

		struct TableDescription
		{
			std::vector<ColumnDescription> columns;
			const char* tableName;
		};

		bool CreateDatabaseTables();
		bool DoesKeyValueTableExist();
		bool DropAllTables();
		bool ValidateTableStructures();
		bool ValidateTableStructure(const char* tableName, const std::vector<ColumnDescription>& tableStructure);

	private:
		std::vector<TableDescription> tableStructures;

	private:
		sqlite3* database;
	};
}