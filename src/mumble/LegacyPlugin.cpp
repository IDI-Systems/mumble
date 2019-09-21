// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "LegacyPlugin.h"

LegacyPlugin::~LegacyPlugin() {
}

void LegacyPlugin::resolveFunctionPointers() {
	QWriteLocker lock(&this->pluginLock);

	if (pluginIsValid) {
		// The corresponding library was loaded -> try to locate all API functions of the legacy plugin's spec
		// (for positional audio) and set defaults for the other ones in order to maintain compatibility with
		// the new plugin system
		
		mumblePluginFunc pluginFunc = reinterpret_cast<mumblePluginFunc>(lib.resolve("getMumblePlugin"));	
		mumblePlugin2Func plugin2Func = reinterpret_cast<mumblePlugin2Func>(lib.resolve("getMumblePlugin2"));	
		mumblePluginQtFunc pluginQtFunc = reinterpret_cast<mumblePluginQtFunc>(lib.resolve("getMumblePluginQt"));	

		if (pluginFunc) {
			mumPlug = pluginFunc();
		}
		if (plugin2Func) {
			mumPlug2 = plugin2Func();
		}
		if (pluginQtFunc) {
			mumPlugQt = pluginQtFunc();
		}

		// A legacy plugin is valid as long as there is a function to get the MumblePlugin struct from it
		// and the plugin has been compiled by the same compiler as this client (determined by the plugin's
		// "magic") and it isn't retracted
		bool suitableMagic = mumPlug && mumPlug->magic == MUMBLE_PLUGIN_MAGIC;
		bool retracted = mumPlug && mumPlug->shortname == L"Retracted";
		pluginIsValid = pluginFunc && suitableMagic && !retracted;

		if (!pluginIsValid) {
			this->setFunctionPointersToNull();

#ifdef MUMBLE_PLUGIN_DEBUG
			if (!pluginFunc) {
				qDebug("Plugin \"%s\" is missing the getMumblePlugin() function", qPrintable(pluginPath));
			} else if (!suitableMagic) {
				qDebug("Plugin \"%s\" was compiled with a different compiler (magic differs)", qPrintable(pluginPath));
			} else {
				qDebug("Plugin \"%s\" is retracted", qPrintable(pluginPath));
			}
#endif

			return;
		}

		// set function pointers to maintain compatibility
		
	} else {
		this->setFunctionPointersToNull();
	}
}

void LegacyPlugin::setFunctionPointersToNull() {
	// Call super function
	Plugin::setFunctionPointersToNull();
}

void LegacyPlugin::setDefaultImplementations() {
	// Call super function
	Plugin::setDefaultImplementations();
}
