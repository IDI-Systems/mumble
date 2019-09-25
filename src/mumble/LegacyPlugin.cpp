// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "LegacyPlugin.h"
#include <cstdlib>
#include <wchar.h>
#include <map>
#include <string.h>
#include <codecvt>
#include <locale>

/// This function converts a wide-string (wstring) to its multibyte-representation by allocating a new
/// char-buffer on the heap and writing to the representation to it.
///
/// @param wStr The reference to the wstring that shall be converted
/// @returns A pointer to the converted representationÄ's char-buffer. This pointer needs to be deleted by the caller
char* convertWString(const std::wstring& wStr) {
	// +1 for the terminating null-byte
	size_t size = std::wcslen(wStr.c_str()) + 1;

	char* buffer = new char[size];

	std::wcstombs(buffer, wStr.c_str(), size);

	buffer[size-1] = '\0';

	return buffer;
}

LegacyPlugin::LegacyPlugin(QString path, QObject *p) : Plugin(path, p), name(0), description(0), context(0), identity(0), oldIdentity() {
}

LegacyPlugin::~LegacyPlugin() {
	delete name;
	delete description;
	delete context;
	delete identity;
}

bool LegacyPlugin::doInitialize() {
	if (Plugin::doInitialize()) {
		// intialization seems to have succeeded so far
		// This means that mumPlug is initialized
		
		// delete name and description in case this function is being invoked a second time for some reason
		// If it is called for the first time name and description should have been initialized to NULL making the
		// delete operation a void operation
		delete name;
		delete description;

		this->name = convertWString(this->mumPlug->shortname);
		// Although the MumblePlugin struct has a member called "description", the actual description seems to
		// always only be returned by the longdesc function (The description member is actually just the name with some version
		// info)
		this->description = convertWString(this->mumPlug->longdesc());

		return true;
	} else {
		// initialization has failed
		this->name = nullptr;
		this->description = nullptr;

		// pass on info about failed init
		return false;
	}
}

void LegacyPlugin::resolveFunctionPointers() {
	// We don't set any functions inside the apiFnc struct variable in order for the default
	// implementations in the Plugin class to mimic empty default implementations for all functions
	// not explicitly overwritten by this class
	QWriteLocker lock(&this->pluginLock);

	if (this->isValid()) {
		// The corresponding library was loaded -> try to locate all API functions of the legacy plugin's spec
		// (for positional audio) and set defaults for the other ones in order to maintain compatibility with
		// the new plugin system
		
		mumblePluginFunc pluginFunc = reinterpret_cast<mumblePluginFunc>(this->lib.resolve("getMumblePlugin"));	
		mumblePlugin2Func plugin2Func = reinterpret_cast<mumblePlugin2Func>(this->lib.resolve("getMumblePlugin2"));	
		mumblePluginQtFunc pluginQtFunc = reinterpret_cast<mumblePluginQtFunc>(this->lib.resolve("getMumblePluginQt"));	

		if (pluginFunc) {
			this->mumPlug = pluginFunc();
		}
		if (plugin2Func) {
			this->mumPlug2 = plugin2Func();
		}
		if (pluginQtFunc) {
			this->mumPlugQt = pluginQtFunc();
		}

		// A legacy plugin is valid as long as there is a function to get the MumblePlugin struct from it
		// and the plugin has been compiled by the same compiler as this client (determined by the plugin's
		// "magic") and it isn't retracted
		bool suitableMagic = this->mumPlug && this->mumPlug->magic == MUMBLE_PLUGIN_MAGIC;
		bool retracted = this->mumPlug && this->mumPlug->shortname == L"Retracted";
		this->pluginIsValid = pluginFunc && suitableMagic && !retracted;

#ifdef MUMBLE_PLUGIN_DEBUG
		if (!this->pluginIsValid) {
			if (!pluginFunc) {
				qDebug("Plugin \"%s\" is missing the getMumblePlugin() function", qPrintable(this->pluginPath));
			} else if (!suitableMagic) {
				qDebug("Plugin \"%s\" was compiled with a different compiler (magic differs)", qPrintable(this->pluginPath));
			} else {
				qDebug("Plugin \"%s\" is retracted", qPrintable(this->pluginPath));
			}
		}
#endif
	}
}

const char* LegacyPlugin::getName() {
	if (this->name) {
		return this->name;
	} else {
		return "Unknown Legacy Plugin";
	}	
}

const char* LegacyPlugin::getDescription() {
	if (this->description) {
		return this->description;
	} else {
		return "No description provided by the legacy plugin";
	}
}


uint8_t LegacyPlugin::initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount) {
	int retCode;

	if (this->mumPlug2) {
		// Create and populate a multimap holding the names and PIDs to pass to the tryLock-function
		std::multimap<std::wstring, unsigned long long int> pidMap;

		for (size_t i=0; i<programCount; i++) {
			std::string currentName = programNames[i];
			std::wstring currentNameWstr = std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(currentName);

			pidMap.insert(std::pair<std::wstring, unsigned long long int>(currentNameWstr, programPIDs[i]));
		}

		retCode = this->mumPlug2->trylock(pidMap);
	} else {
		// The default MumblePlugin doesn't take the name and PID arguments
		retCode = this->mumPlug->trylock();
	}

	// ensure that only expected return codes are being returned from this function
	// the legacy plugins return 1 on successfull locking and 0 on failure
	if (retCode) {
		return PDEC_OK;
	} else {
		// legacy plugins don't have the concept of indicating a permanent error
		// so we'll return a temporary error for them
		return PDEC_ERROR_TEMP;
	}
}

bool LegacyPlugin::fetchPositionalData(float *avatar_pos, float *avatar_front, float *avatar_axis, float *camera_pos, float *camera_front,
		float *camera_axis, const char **context, const char **identity) {
	std::wstring identityWstr;
	std::string contextStr;

	int retCode = this->mumPlug->fetch(avatar_pos, avatar_front, avatar_axis, camera_pos, camera_front, camera_axis, contextStr, identityWstr);

	if (strcmp(contextStr.c_str(), this->context) != 0) {
		// The context has changed -> delete the old one and replace it with the new one
		delete this->context;
		this->context = new char[contextStr.size() + 1];
		strcpy(this->context, contextStr.c_str());
		this->context[contextStr.size()] = '\0';
	}
	*context = this->context;

	if (oldIdentity != identityWstr) {
		// The identity has changed -> delete the old one and replace it with the new one
		delete this->identity;
		this->identity = convertWString(identityWstr);
	}
	*identity = this->identity;

	// The fetch-function should return if it is "still locked on" meaning that it can continue providing
	// positional audio
	return retCode == 1;
}

void LegacyPlugin::shutdownPositionalData() {
	this->mumPlug->unlock();
}
