#include "GameAnalyticsDatabase.h"

#include <json/json.h>
#include <sqlite/sqlite3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <assert.h>

using namespace Analytics;

GameAnalyticsDatabase::GameAnalyticsDatabase()
	: database(NULL)
{
}

GameAnalyticsDatabase::~GameAnalyticsDatabase()
{
	sqlite3_close(database);
	database = NULL;
}

Result::Enum GameAnalyticsDatabase::Initialize(const char* databaseFile)
{
	int rc = sqlite3_open(databaseFile, &database);
	if (rc != SQLITE_OK)
		return Result::CannotOpenDatabase;

	if (sqlite3_db_readonly(database, NULL) == 1)
	{
		sqlite3_close(database);
		database = NULL;
		return Result::DatabaseIsReadonly;
	}

	{
		// Make sure all this matches with CreateDatabaseTables()
		tableStructures.clear();

		TableDescription eventsTableStructure;
		eventsTableStructure.tableName = "events";
		//eventsTableStructure.columns.push_back(ColumnDescription("id", "INTEGER", true, true));
		eventsTableStructure.columns.push_back(ColumnDescription("json", "TEXT", true));
		eventsTableStructure.columns.push_back(ColumnDescription("is_sent", "INTEGER", true));
		tableStructures.push_back(eventsTableStructure);

		TableDescription keyValueTableStructure;
		keyValueTableStructure.tableName = "key_value";
		keyValueTableStructure.columns.push_back(ColumnDescription("key", "TEXT", true, true));
		keyValueTableStructure.columns.push_back(ColumnDescription("value", "INTEGER"));
		tableStructures.push_back(keyValueTableStructure);

		TableDescription progressionTableStructure;
		progressionTableStructure.tableName = "progression";
		progressionTableStructure.columns.push_back(ColumnDescription("progression_event_id", "TEXT", true, true, false));
		progressionTableStructure.columns.push_back(ColumnDescription("attempt_num", "INTEGER", true, false, true, 0));
		tableStructures.push_back(progressionTableStructure);

		TableDescription sessionEndTableStructure;
		sessionEndTableStructure.tableName = "session_end";
		sessionEndTableStructure.columns.push_back(ColumnDescription("session_start_ts", "INTEGER", true));
		sessionEndTableStructure.columns.push_back(ColumnDescription("session_id", "TEXT", true, true));
		sessionEndTableStructure.columns.push_back(ColumnDescription("last_event_default_annotations", "TEXT", true));
		tableStructures.push_back(sessionEndTableStructure);
	}

	if (!ValidateTableStructures())
	{
		// Start with a clean database
		DropAllTables();

		if (!CreateDatabaseTables())
		{
			sqlite3_close(database);
			database = NULL;
			return Result::CannotCreateTables;
		}
	}

	return Result::Ok;
}

bool GameAnalyticsDatabase::IsInitialized() const
{
	return (database != NULL);
}

bool GameAnalyticsDatabase::GetAllSessionEnds(std::vector<SessionEndData>& outSessionEndData) const
{
	std::string statementStr = "SELECT `session_start_ts`, `session_id`, `last_event_default_annotations` FROM session_end;";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	while ((rc = sqlite3_step(statement)) == SQLITE_ROW)
	{
		assert(sqlite3_column_count(statement) == 3);

		SessionEndData session;
		session.sessionStartTimestamp = sqlite3_column_int64(statement, 0);
		session.sessionId = (const char*)sqlite3_column_text(statement, 1);
		session.annotations = (const char*)sqlite3_column_text(statement, 2);
		outSessionEndData.push_back(session);
	}

	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

int GameAnalyticsDatabase::GetNumSessions() const
{
	int session_num = 0;
	GetKeyValuePair("session_num", session_num);
	return session_num;
}

void GameAnalyticsDatabase::SetNumSessions(int sessions)
{
	SetKeyValuePair("session_num", sessions);
}

bool GameAnalyticsDatabase::SetKeyValuePair(const char* key, int value)
{
	// Updates or creates a new key with value
	std::string statementStr = "INSERT OR REPLACE INTO key_value(key, value) VALUES(?, ?);";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_text(statement, 1, key, -1, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_int(statement, 2, value);
	assert(rc == SQLITE_OK);

	rc = sqlite3_step(statement);
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	return (rc == SQLITE_OK);
}

bool GameAnalyticsDatabase::GetKeyValuePair(const char* key, int& outValue) const
{
	std::string statementStr = "SELECT value FROM key_value WHERE key = ?;";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_text(statement, 1, key, -1, NULL);
	assert(rc == SQLITE_OK);

	while ((rc = sqlite3_step(statement)) == SQLITE_ROW)
	{
		assert(sqlite3_column_count(statement) == 1);
		outValue = sqlite3_column_int(statement, 0);
	}

	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

bool GameAnalyticsDatabase::AddEvent(const char* eventData)
{
	std::string statementStr = "INSERT INTO `events` (`json`) VALUES (?);";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_text(statement, 1, eventData, -1, NULL);
	assert(rc == SQLITE_OK);

	rc = sqlite3_step(statement);
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	return (rc == SQLITE_OK);
}

bool GameAnalyticsDatabase::FlagEvents(int requestId, int amount)
{
	// The last piece of this query makes sure it is sorted by id
	// Change the limit to send more/less events in a single post
	std::string updateStatementStr = "UPDATE `events` SET `is_sent` = ? WHERE `_rowid_` IN (SELECT `_rowid_` FROM `events` WHERE `is_sent` = 0 ORDER BY `_rowid_` ASC LIMIT ?)";
	sqlite3_stmt* updateStatement = NULL;
	int rc = sqlite3_prepare_v2(database, updateStatementStr.c_str(), -1, &updateStatement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_int(updateStatement, 1, requestId);
	assert(rc == SQLITE_OK);

	rc = sqlite3_bind_int(updateStatement, 2, amount);
	assert(rc == SQLITE_OK);

	rc = sqlite3_step(updateStatement);
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(updateStatement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

bool GameAnalyticsDatabase::UnflagEvents(int requestId)
{
	std::string updateStatementStr = "UPDATE `events` SET `is_sent` = 0 WHERE `is_sent` = ?;";
	sqlite3_stmt* updateStatement = NULL;
	int rc = sqlite3_prepare_v2(database, updateStatementStr.c_str(), -1, &updateStatement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_int(updateStatement, 1, requestId);
	assert(rc == SQLITE_OK);

	rc = sqlite3_step(updateStatement);
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(updateStatement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

bool Analytics::GameAnalyticsDatabase::UnflagAllEvents()
{
	std::string updateStatementStr = "UPDATE `events` SET `is_sent` = 0;";
	sqlite3_stmt* updateStatement = NULL;
	int rc = sqlite3_prepare_v2(database, updateStatementStr.c_str(), -1, &updateStatement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_step(updateStatement);
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(updateStatement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

bool GameAnalyticsDatabase::RetrieveFlaggedEvents(int requestId, Json::Value& outJson, long long serverTimeDifference) const
{
	// Retrieve all events that have been flagged as sent
	std::string statementStr = "SELECT `json` FROM `events` WHERE `is_sent` = ? ORDER BY `_rowid_` ASC;";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_int(statement, 1, requestId);
	assert(rc == SQLITE_OK);

	while ((rc = sqlite3_step(statement)) == SQLITE_ROW)
	{
		assert(sqlite3_column_count(statement) == 1);
		const char* text = (const char*)sqlite3_column_text(statement, 0);

		Json::Reader reader;
		Json::Value root;
		if (!reader.parse(text, root, false))
			continue;

		// Convert time to server time
		assert(root.get("client_ts", Json::nullValue).isInt());
		root["client_ts"] = root.get("client_ts", 0).asInt64() + serverTimeDifference;

		outJson.append(root);
	}

	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

bool GameAnalyticsDatabase::DeleteFlaggedEvents(int requestId)
{
	std::string statementStr = "DELETE FROM `events` WHERE `is_sent` = ?;";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_int(statement, 1, requestId);
	assert(rc == SQLITE_OK);

	rc = sqlite3_step(statement);
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

bool GameAnalyticsDatabase::GetProgressionAttempts(const char* progressionEventId, int& outAttempts)
{
	std::string statementStr = "SELECT `attempt_num` FROM `progression` WHERE `progression_event_id` = ?;";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_text(statement, 1, progressionEventId, -1, NULL);
	assert(rc == SQLITE_OK);

	while ((rc = sqlite3_step(statement)) == SQLITE_ROW)
	{
		assert(sqlite3_column_count(statement) == 1);
		outAttempts = sqlite3_column_int(statement, 0);
	}

	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

bool GameAnalyticsDatabase::SetProgressionAttempts(const char* progressionEventId, int attempts)
{
	std::string statementStr = "INSERT OR REPLACE INTO `progression` (`progression_event_id`, `attempt_num`) VALUES(?, ?);";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	assert(rc == SQLITE_OK);

	rc = sqlite3_bind_text(statement, 1, progressionEventId, -1, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_int(statement, 2, attempts);
	assert(rc == SQLITE_OK);

	rc = sqlite3_step(statement);
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	return (rc == SQLITE_OK);
}

bool GameAnalyticsDatabase::DeleteProgressionAttempts(const char* progressionEventId)
{
	std::string statementStr = "DELETE FROM `progression` WHERE `progression_event_id` = ?;";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	rc = sqlite3_bind_text(statement, 1, progressionEventId, -1, NULL);
	assert(rc == SQLITE_OK);

	rc = sqlite3_step(statement);
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	if (rc != SQLITE_OK)
		return false;

	return true;
}

bool GameAnalyticsDatabase::UpdateSessionEnds(const Json::Value& eventData, const char* jsonString, long long sessionStartTimestamp)
{
	assert(eventData.get("client_ts", Json::nullValue).isInt());
	assert(eventData.get("session_id", Json::nullValue).isString());

	if (eventData.get("category", Json::nullValue).asString() == "session_end")
	{
		// This is a session_end, so remove from database
		std::string statementStr = "DELETE FROM `session_end` WHERE `session_id` = ?;";

		sqlite3_stmt* statement = NULL;
		int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
		if (rc != SQLITE_OK)
			return false;

		std::string sessionID = eventData.get("session_id", "").asString();
		rc = sqlite3_bind_text(statement, 1, sessionID.c_str(), -1, NULL);
		assert(rc == SQLITE_OK);

		rc = sqlite3_step(statement);
		if (rc != SQLITE_DONE)
			return false;

		rc = sqlite3_finalize(statement);
		return (rc == SQLITE_OK);
	}
	else
	{
		// Updates or creates a new session to end
		std::string statementStr = "INSERT OR REPLACE INTO `session_end` (`session_start_ts`, `session_id`, `last_event_default_annotations`) VALUES(?, ?, ?);";

		sqlite3_stmt* statement = NULL;
		int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
		if (rc != SQLITE_OK)
			return false;

		// If this is not cached it sends garbage for some reason..
		// Maybe sqlite's defines do something funky
		std::string sessionID = eventData.get("session_id", "").asString();

		rc = sqlite3_bind_int64(statement, 1, sessionStartTimestamp);
		assert(rc == SQLITE_OK);
		rc = sqlite3_bind_text(statement, 2, sessionID.c_str(), -1, NULL);
		assert(rc == SQLITE_OK);
		rc = sqlite3_bind_text(statement, 3, jsonString, -1, NULL);
		assert(rc == SQLITE_OK);

		rc = sqlite3_step(statement);
		if (rc != SQLITE_DONE)
			return false;

		rc = sqlite3_finalize(statement);
		return (rc == SQLITE_OK);
	}
}

bool GameAnalyticsDatabase::CreateDatabaseTables()
{
	std::string query = "";
	query += "CREATE TABLE `events` (`json` TEXT NOT NULL, `is_sent` INTEGER NOT NULL DEFAULT 0);";
	query += "CREATE TABLE `key_value` (`key` TEXT NOT NULL UNIQUE, `value` INTEGER, PRIMARY KEY(key));";
	query += "CREATE TABLE `session_end` ( `session_start_ts` INTEGER NOT NULL, `session_id` TEXT NOT NULL UNIQUE, `last_event_default_annotations` TEXT NOT NULL, PRIMARY KEY(session_id));";
	query += "CREATE TABLE `progression` (`progression_event_id` TEXT NOT NULL UNIQUE, `attempt_num` INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(progression_event_id));";

	char* errorMessage = NULL;
	int success = sqlite3_exec(database, query.c_str(), NULL, NULL, &errorMessage);
	if (success != SQLITE_OK)
	{
		OutputDebugStringA(errorMessage);
		sqlite3_free(errorMessage);
	}

	return (success == SQLITE_OK);
}

bool GameAnalyticsDatabase::DoesKeyValueTableExist()
{
	std::string query = "SELECT name FROM sqlite_master WHERE type='table' AND name='key_value';";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, query.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	int foundRows = 0;
	while ((rc = sqlite3_step(statement)) == SQLITE_ROW)
	{
		++foundRows;
	}
	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	if (rc != SQLITE_OK)
		return false;

	return (foundRows > 0);
}

bool GameAnalyticsDatabase::DropAllTables()
{
	for (size_t i = 0; i < tableStructures.size(); ++i)
	{
		std::string statementStr = "DROP TABLE IF EXISTS `" + std::string(tableStructures[i].tableName) + "`;";

		sqlite3_stmt* statement = NULL;
		int rc = sqlite3_prepare_v2(database, statementStr.c_str(), -1, &statement, NULL);
		if (rc != SQLITE_OK)
		{
			OutputDebugStringA(sqlite3_errmsg(database));
			return false;
		}

		rc = sqlite3_step(statement);
		if (rc != SQLITE_DONE)
			return false;

		rc = sqlite3_finalize(statement);
		if (rc != SQLITE_OK)
			return false;
	}
	return true;
}

bool GameAnalyticsDatabase::ValidateTableStructures()
{
	for (size_t i = 0; i < tableStructures.size(); ++i)
	{
		if (!ValidateTableStructure(tableStructures[i].tableName, tableStructures[i].columns))
			return false;
	}

	return true;
}

bool GameAnalyticsDatabase::ValidateTableStructure(const char* tableName, const std::vector<ColumnDescription>& tableStructure)
{
	std::string query = "PRAGMA `table_info`(`" + std::string(tableName) + "`);";

	sqlite3_stmt* statement = NULL;
	int rc = sqlite3_prepare_v2(database, query.c_str(), -1, &statement, NULL);
	if (rc != SQLITE_OK)
		return false;

	bool failedCheck = false;
	bool foundRows = false;
	size_t fieldId = 0;
	for ((rc = sqlite3_step(statement)); rc == SQLITE_ROW; rc = sqlite3_step(statement), ++fieldId)
	{
		foundRows = true;
		if (sqlite3_column_count(statement) != 6 || fieldId >= tableStructure.size())
		{
			failedCheck = true;
			continue;
		}

		if (strcmp(tableStructure[fieldId].name, (const char*)sqlite3_column_text(statement, 1)) != 0)
		{
			failedCheck = true;
			continue;
		}

		if (strcmp(tableStructure[fieldId].type, (const char*)sqlite3_column_text(statement, 2)) != 0)
		{
			failedCheck = true;
			continue;
		}

		if (tableStructure[fieldId].notNull != (sqlite3_column_int(statement, 3) > 0))
		{
			failedCheck = true;
			continue;
		}

		if (tableStructure[fieldId].defaultValue != sqlite3_column_int(statement, 4))
		{
			failedCheck = true;
			continue;
		}

		if (tableStructure[fieldId].primaryKey != (sqlite3_column_int(statement, 5) > 0))
		{
			failedCheck = true;
			continue;
		}
	}

	if (rc != SQLITE_DONE)
		return false;

	rc = sqlite3_finalize(statement);
	if (rc != SQLITE_OK)
		return false;

	return !failedCheck && foundRows;
}
