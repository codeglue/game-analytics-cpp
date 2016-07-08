# Game Analytics C++
The purpose of this project is to add a [GameAnalytics](http://www.gameanalytics.com/) interface for C++ projects because there is no such project publicly available and functional at this moment.

Game Analytics C++ uses GameAnalytics' REST API based on documentation provided here: [Collector REST API Reference](http://restapidocs.gameanalytics.com/)

This project contains working code for Universal Windows Platform (Windows 10, Windows Phone 10) projects as well as standalone C++ projects so it can be integrated in eg. Steam games.

## Supported platforms
Right now this project works on these platforms
- Any 32-bit C++ Windows program (eg. to release on Steam)
- Windows 10 Store Apps (Universal Windows Platform)
 - Desktop
 - Mobile (ARM)

## Features
- Session events
- (Custom) Design events
- Progression events
- Works when user is offline by caching unsent events
- Throttling sending of events when too many events are sent
- Secure https connection

## Dependencies
This project comes bundled with all its dependencies, so it should work out of the box by opening the solution file.
- [JSON](https://github.com/open-source-parsers/jsoncpp)
- [SQLite](https://www.sqlite.org/)
- [Crypto++](https://www.cryptopp.com/) (for non-UWP)
- [libCurl](https://curl.haxx.se/libcurl/) (for non-UWP)

## Usage
Add all projects from `GameAnalytics.sln` to your own solution and add `GameAnalytics` or `GameAnalytics_UWP` (for UWP projects) as a dependency to your project.

### Initializing
Create an instance of the `GameAnalytics` class and pass along your game's secret key and game key which are provided by [GameAnalytics](http://www.gameanalytics.com/). Then call `Init` on the class and pass along an `InitData` struct with contains the current build name of your application, a user identifier which is unique for each user (eg. a Steam ID), and the filename of the database where all events will be cached (eg. `stats.db`). You might also want to immediately send the session start event when your application starts. The session start event has to be sent before any other events can be sent.
```C++
void InitGame()
{
	gameAnalytics = new Analytics::GameAnalytics(SecretKey, GameKey);
	Analytics::GameAnalytics::InitData initData;
	initData.buidName = "v1.0.0"
	initData.userId = GetUserID();
	initData.databaseFileName = "stats.db";
	gameAnalytics->Init(initData);
	gameAnalytics->SendSessionStartEvent();
}
```

### Updating
The `GameAnalytics` instance needs to be continuously updated in order for it to send cached events to the GameAnalytics REST interface. In a game you would generally call it every game tick, and pass along the time since last update in seconds.
```C++
void GameLoopUpdate(float deltaTime)
{
  gameAnalytics->Update(deltaTime);
}
```

### Deinitializing
When you're done using GameAnalytics, simply delete the instance and it will wait for all threads to stop
```C++
delete gameAnalytics;
```

### Sending design events
You can send design events with and without a value by using `GameAnalytics::SendDesignEvent()`.
```C++
void OnKilledSmurf(int score)
{
	gameAnalytics->SendDesignEvent("GamePlay:Kill:AlienSmurf", 10);
}

void OnVolumeChanged(bool volumeOn)
{
	if (volumeOn)
		gameAnalytics->SendDesignEvent("GuiClick:Volume:On");
	else
		gameAnalytics->SendDesignEvent("GuiClick:Volume:Off");
}
```

### Sending progression events
You can send progression events with a score by using `GameAnalytics::SendProgressionEvent()` and pass a `ProgressionStatus` along such as `Start`, `Fail` and `Complete`.
Every started progression is stored in a database and the amount of tries is incremented with each `Fail` status. The progression is only removed when a `Complete` status is sent.
```C++
void OnLevelStart()
{
	gameAnalytics->SendDesignEvent(GameAnalytics::ProgressionStatus::Start, "Campaign:Level1");
}

void OnLevelCompleted()
{
	gameAnalytics->SendDesignEvent(GameAnalytics::ProgressionStatus::Complete, "Campaign:Level1");
}

void OnDeath()
{
	gameAnalytics->SendDesignEvent(GameAnalytics::ProgressionStatus::Fail, "Campaign:Level1");
}

void OnQuit()
{
	gameAnalytics->SendDesignEvent(GameAnalytics::ProgressionStatus::Fail, "Campaign:Level1");
}
```

# References
- http://www.gameanalytics.com/docs/ga-data
- http://restapidocs.gameanalytics.com/
- http://jasonericson.blogspot.nl/2013/03/game-analytics-in-c.html
- https://github.com/npruehs/game-analytics-win10
