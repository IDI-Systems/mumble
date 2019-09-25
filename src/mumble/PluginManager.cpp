// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PluginManager.h"
#include "LegacyPlugin.h"
#include <QtCore/QReadLocker>
#include <QtCore/QWriteLocker>
#include <QtCore/QDir>
#include <QtCore/QFileInfoList>
#include <QtCore/QFileInfo>

PluginManager::PluginManager(QString sysPath, QString userPath, QObject *p) : QObject(p), pluginListLock(QReadWriteLock::Recursive), pluginList(),
	systemPluginsPath(sysPath), userPluginsPath(userPath) {
	// Set the paths to read plugins from

#ifdef Q_OS_WIN
	// According to MS KB Q131065, we need this to OpenProcess()

	hToken = NULL;

	if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken)) {
		if (GetLastError() == ERROR_NO_TOKEN) {
			ImpersonateSelf(SecurityImpersonation);
			OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken);
		}
	}

	TOKEN_PRIVILEGES tp;
	LUID luid;
	cbPrevious=sizeof(TOKEN_PRIVILEGES);

	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid);

	tp.PrivilegeCount           = 1;
	tp.Privileges[0].Luid       = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &tpPrevious, &cbPrevious);
#endif
}

PluginManager::~PluginManager() {
	clearPlugins();

#ifdef Q_OS_WIN
	AdjustTokenPrivileges(hToken, FALSE, &tpPrevious, cbPrevious, NULL, NULL);
	CloseHandle(hToken);
#endif
}

void PluginManager::clearPlugins() {
	QWriteLocker lock(&this->pluginListLock);

	// Clear the list itself
	pluginList.clear();
}

#define LOG_FOUND(plugin, path, legacyStr) qDebug("Found %splugin '%s' at \"%s\"", legacyStr, plugin->getName(), qPrintable(path))
#define LOG_FOUND_PLUGIN(plugin, path) LOG_FOUND(plugin, path, "")
#define LOG_FOUND_LEGACY_PLUGIN(plugin, path) LOG_FOUND(plugin, path, "legacy ")
void PluginManager::rescanPlugins() {
	this->clearPlugins();

	QDir sysDir(systemPluginsPath);
	QDir userDir(userPluginsPath);

	// iterate over all files in the respective directories and try to construct a plugin from them
	for (int i=0; i<2; i++) {
		QFileInfoList currentList = (i == 0) ? sysDir.entryInfoList() : userDir.entryInfoList();

		for (int k=0; k<currentList.size(); k++) {
			QFileInfo currentInfo = currentList[k];

			if (!QLibrary::isLibrary(currentInfo.absoluteFilePath())) {
				// consider only files that actually could be libraries
				continue;
			}
			
			try {
				QSharedPointer<Plugin> p(Plugin::createNew<Plugin>(currentInfo.absoluteFilePath()));

#ifdef MUMBLE_PLUGIN_DEBUG
				LOG_FOUND_PLUGIN(p, currentInfo.absoluteFilePath());
#endif

				// if this code block is reached, the plugin was instantiated successfully so we can add it to the list
				pluginList.append(p);
			} catch(const PluginError& e) {
				// If an exception is thrown, this library does not represent a proper plugin
				// Check if it might be a legacy plugin instead
				try {
					QSharedPointer<LegacyPlugin> lp(Plugin::createNew<LegacyPlugin>(currentInfo.absoluteFilePath()));
					
#ifdef MUMBLE_PLUGIN_DEBUG
					LOG_FOUND_LEGACY_PLUGIN(lp, currentInfo.absoluteFilePath());
#endif
				} catch(const PluginError& e) {
					qWarning() << "Non-plugin library in plugin directory:" << currentInfo.absoluteFilePath();
				}
			}
		}
	}
};
