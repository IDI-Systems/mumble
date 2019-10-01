// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PluginManager.h"
#include "LegacyPlugin.h"
#include <QtCore/QReadLocker>
#include <QtCore/QWriteLocker>
#include <QtCore/QReadLocker>
#include <QtCore/QDir>
#include <QtCore/QFileInfoList>
#include <QtCore/QFileInfo>

#include "ManualPlugin.h"

PluginManager::PluginManager(QString sysPath, QString userPath, QObject *p) : QObject(p), pluginCollectionLock(QReadWriteLock::Recursive),
	pluginHashMap(), systemPluginsPath(sysPath), userPluginsPath(userPath) {
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
	QWriteLocker lock(&this->pluginCollectionLock);

	// Clear the list itself
	pluginHashMap.clear();
}

#define LOG_FOUND(plugin, path, legacyStr) qDebug("Found %splugin '%s' at \"%s\"", legacyStr, qUtf8Printable(plugin->getName()), qUtf8Printable(path));\
	qDebug() << "Its description:" << qUtf8Printable(plugin->getDescription())
#define LOG_FOUND_PLUGIN(plugin, path) LOG_FOUND(plugin, path, "")
#define LOG_FOUND_LEGACY_PLUGIN(plugin, path) LOG_FOUND(plugin, path, "legacy ")
#define LOG_FOUND_BUILTIN(plugin) LOG_FOUND(plugin, QString::fromUtf8("<builtin>"), "built-in ")
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

				// if this code block is reached, the plugin was instantiated successfully so we can add it to the map
				pluginHashMap.insert(p->getID(), p);
			} catch(const PluginError& e) {
				// If an exception is thrown, this library does not represent a proper plugin
				// Check if it might be a legacy plugin instead
				try {
					QSharedPointer<LegacyPlugin> lp(Plugin::createNew<LegacyPlugin>(currentInfo.absoluteFilePath()));
					
#ifdef MUMBLE_PLUGIN_DEBUG
					LOG_FOUND_LEGACY_PLUGIN(lp, currentInfo.absoluteFilePath());
#endif
					pluginHashMap.insert(lp->getID(), lp);
				} catch(const PluginError& e) {
					qWarning() << "Non-plugin library in plugin directory:" << currentInfo.absoluteFilePath();
				}
			}
		}
	}

	// handle built-in plugins
#ifdef USE_MANUAL_PLUGIN
	try {
		QSharedPointer<ManualPlugin> mp(Plugin::createNew<ManualPlugin>());

		pluginHashMap.insert(mp->getID(), mp);
#ifdef MUMBLE_PLUGIN_DEBUG
		LOG_FOUND_BUILTIN(mp);
#endif
	} catch(const PluginError& e) {
		qCritical() << "Failed at loading manual plugin:" << e.what();
	}
#endif
}

const QSharedPointer<const Plugin> PluginManager::getPlugin(uint32_t pluginID) const {
	QReadLocker lock(&this->pluginCollectionLock);
	
	return this->pluginHashMap.value(pluginID, nullptr);
}

void PluginManager::checkForPluginUpdates() const {
	// TODO
}

bool PluginManager::fetchPositionalData() {
	// TODO
	return false;
}

void PluginManager::unlinkPositionalData() {
	// TODO
}

bool PluginManager::isPositionalDataAvailable() const {
	QReadLocker lock(&this->activePosDataPluginLock);

	return this->activePositionalDataPlugin != nullptr;
}

const PositionalData& PluginManager::getPositionalData() const {
	return this->positionalData;
}

void PluginManager::enablePositionalDataFor(const QSharedPointer<const Plugin>& plugin, bool enable) const {
	// TODO
}

const QVector<QSharedPointer<const Plugin> > PluginManager::getPlugins(bool sorted) const {
	QReadLocker lock(&this->pluginCollectionLock);

	QVector<QSharedPointer<const Plugin>> pluginList;

	QHash<uint32_t, QSharedPointer<Plugin>>::const_iterator it = this->pluginHashMap.constBegin();
	if (sorted) {
		QList ids = this->pluginHashMap.keys();

		// sort keys so that the corresponding Plugins are in alphabetical order based on their name
		std::sort(ids.begin(), ids.end(), [this](uint32_t first, uint32_t second) {
			return QString::compare(this->pluginHashMap.value(first)->getName(), this->pluginHashMap.value(second)->getName(),
					Qt::CaseInsensitive) <= 0;
		});

		foreach(uint32_t currentID, ids) {
			pluginList.append(this->pluginHashMap.value(currentID));
		}
	} else {
		while (it != this->pluginHashMap.constEnd()) {
			pluginList.append(it.value());

			it++;
		}
	}

	return pluginList;
}
