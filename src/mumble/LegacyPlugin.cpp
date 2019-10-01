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

LegacyPlugin::LegacyPlugin(QString path, bool isBuiltIn, QObject *p) : Plugin(path, isBuiltIn, p), name(), description(), context(0), identity(0),
	oldIdentity(), mumPlug(0), mumPlug2(0), mumPlugQt(0) {
}

LegacyPlugin::~LegacyPlugin() {
	delete context;
	delete identity;
}

bool LegacyPlugin::doInitialize() {
	if (Plugin::doInitialize()) {
		// initialization seems to have succeeded so far
		// This means that mumPlug is initialized
		
		this->name = QString::fromStdWString(this->mumPlug->shortname);
		// Although the MumblePlugin struct has a member called "description", the actual description seems to
		// always only be returned by the longdesc function (The description member is actually just the name with some version
		// info)
		this->description = QString::fromStdWString(this->mumPlug->longdesc());

		return true;
	} else {
		// initialization has failed
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

QString LegacyPlugin::getName() const {
	if (!this->name.isEmpty()) {
		return this->name;
	} else {
		return QString::fromUtf8("Unknown Legacy Plugin");
	}	
}

QString LegacyPlugin::getDescription() const {
	if (!this->description.isEmpty()) {
		return this->description;
	} else {
		return QString::fromUtf8("No description provided by the legacy plugin");
	}
}

bool LegacyPlugin::showAboutDialog(QWidget *parent) const {
	if (this->mumPlugQt && this->mumPlugQt->about) {
		this->mumPlugQt->about(parent);

		return true;
	}
	if (this->mumPlug->about) {
		// the original implementation in Mumble would pass nullptr to the about-function in the mumPlug struct
		// so we'll mimic that behaviour for compatibility
		this->mumPlug->about(nullptr);

		return true;
	}

	return false;
}

bool LegacyPlugin::showConfigDialog(QWidget *parent) const {
	if (this->mumPlugQt && this->mumPlugQt->config) {
		this->mumPlugQt->config(parent);

		return true;
	}
	if (this->mumPlug->config) {
		// the original implementation in Mumble would pass nullptr to the about-function in the mumPlug struct
		// so we'll mimic that behaviour for compatibility
		this->mumPlug->config(nullptr);

		return true;
	}

	return false;
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

bool LegacyPlugin::fetchPositionalData(float *avatarPos, float *avatarDir, float *avatarAxis, float *cameraPos, float *cameraDir,
		float *cameraAxis, const char **context, const char **identity) {
	std::wstring identityWstr;
	std::string contextStr;

	int retCode = this->mumPlug->fetch(avatarPos, avatarDir, avatarAxis, cameraPos, cameraDir, cameraAxis, contextStr, identityWstr);

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

bool LegacyPlugin::providesAboutDialog() const {
	return this->mumPlug->about || (this->mumPlugQt && this->mumPlugQt->about);
}

bool LegacyPlugin::providesConfigDialog() const {
	return this->mumPlug->config || (this->mumPlugQt && this->mumPlugQt->config);
}
