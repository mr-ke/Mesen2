#pragma once

#include "pch.h"

#include <unordered_map>
#include "Utilities/SimpleLock.h"

#ifdef _DEBUG
	#define LogDebug(msg) MessageManager::Log(msg);
	#define LogDebugIf(cond, msg) if(cond) { MessageManager::Log(msg); }
#else
	#define LogDebug(msg) 
	#define LogDebugIf(cond, msg)
#endif

class MessageManager
{
private:
	static std::unordered_map<string, string> _enResources;

	static bool _osdEnabled;
	static bool _outputToStdout;
	static SimpleLock _logLock;
	static std::list<string> _log;
	
public:
	static void SetOptions(bool osdEnabled, bool outputToStdout);

	static string Localize(string key);

	static void DisplayMessage(string title, string message, string param1 = "", string param2 = "");

	static void Log(string message = "");
	static void ClearLog();
	static string GetLog();
};
