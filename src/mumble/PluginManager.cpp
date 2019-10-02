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

#ifdef Q_OS_WIN
	#include <tlhelp32.h>
	#include <string>
#endif

PluginManager::PluginManager(QString sysPath, QString userPath, QObject *p) : QObject(p), pluginCollectionLock(QReadWriteLock::Recursive),
	pluginHashMap(), systemPluginsPath(sysPath), userPluginsPath(userPath), positionalData(), activePosDataPluginLock(QReadWriteLock::Recursive),
		activePositionalDataPlugin() {
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

/// Fills the given map with currently running programs by adding their PID and their name
///
/// @param[out] pids The QMultiMap to write the gathered info to
void getProgramPIDs(QMultiMap<QString, unsigned long long int> pids) {
#if defined(Q_OS_WIN)
	PROCESSENTRY32 pe;

	pe.dwSize = sizeof(pe);
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnap != INVALID_HANDLE_VALUE) {
		BOOL ok = Process32First(hSnap, &pe);

		while (ok) {
			pids.insert(QString::fromStdWString(std::wstring(pe.szExeFile)), pe.th32ProcessID);
			ok = Process32Next(hSnap, &pe);
		}
		CloseHandle(hSnap);
	}
#elif defined(Q_OS_LINUX)
	QDir d(QLatin1String("/proc"));
	QStringList entries = d.entryList();
	bool ok;
	foreach (const QString &entry, entries) {
		// Check if the entry is a PID
		// by checking whether it's a number.
		// If it is not, skip it.
		unsigned long long int pid = static_cast<unsigned long long int>(entry.toLongLong(&ok, 10));
		if (!ok) {
			continue;
		}

		QString exe = QFile::symLinkTarget(QString(QLatin1String("/proc/%1/exe")).arg(entry));
		QFileInfo fi(exe);
		QString firstPart = fi.baseName();
		QString completeSuffix = fi.completeSuffix();
		QString baseName;
		if (completeSuffix.isEmpty()) {
			baseName = firstPart;
		} else {
			baseName = firstPart + QLatin1String(".") + completeSuffix;
		}

		if (baseName == QLatin1String("wine-preloader") || baseName == QLatin1String("wine64-preloader")) {
			QFile f(QString(QLatin1String("/proc/%1/cmdline")).arg(entry));
			if (f.open(QIODevice::ReadOnly)) {
				QByteArray cmdline = f.readAll();
				f.close();

				int nul = cmdline.indexOf('\0');
				if (nul != -1) {
					cmdline.truncate(nul);
				}

				QString exe = QString::fromUtf8(cmdline);
				if (exe.contains(QLatin1String("\\"))) {
					int lastBackslash = exe.lastIndexOf(QLatin1String("\\"));
					if (exe.count() > lastBackslash + 1) {
						baseName = exe.mid(lastBackslash + 1);
					}
				}
			}
		}

		if (!baseName.isEmpty()) {
			pids.insert(baseName, pid);
		}
	}
#endif
}

bool PluginManager::selectActivePositionalDataPlugin() {
	QReadLocker pluginLock(&this->pluginCollectionLock);
	QWriteLocker activePluginLock(&this->activePosDataPluginLock);

	QHash<uint32_t, QSharedPointer<Plugin>>::iterator it = this->pluginHashMap.begin();

	// gather PIDs and names of currently running programs
	QMultiMap<QString, unsigned long long int> pidMap;
	getProgramPIDs(pidMap);

	const char* names[pidMap.size()];
	uint64_t pids[pidMap.size()];

	unsigned int index = 0;
	// split the MultiMap into the two arrays
	QMultiMap<QString, unsigned long long int>::const_iterator pidIter = pidMap.constBegin();
	while (pidIter != pidMap.constEnd()) {
		names[index] = pidIter.key().toUtf8().constData();
		pids[index] = pidIter.value();

		pidIter++;
		index++;
	}

	// We assume that there is only one (enabled) plugin for the currently played game so we don't have to remember
	// which plugin was active last
	while (it != this->pluginHashMap.end()) {
		QSharedPointer<Plugin> currentPlugin = it.value();

		if (currentPlugin->isPositionalDataEnabled()) {
			switch(currentPlugin->initPositionalData(names, pids, pidMap.size())) {
				case PDEC_OK:
					// the plugin is ready to provide positional data
					this->activePositionalDataPlugin = currentPlugin;
					return true;

				case PDEC_ERROR_PERM:
					// the plugin encountered a permanent error -> disable it
					currentPlugin->enablePositionalData(false);
					break;

				// Default: The plugin encountered a temporary error -> skip it for now (that is: do nothing)
			}
		}

		it++;
	}

	this->activePositionalDataPlugin = QSharedPointer<Plugin>();

	return false;
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
	QReadLocker activePluginLock(&this->activePosDataPluginLock);

	if (!this->activePositionalDataPlugin) {
		// unlock the read-lock in order to allow selectActivePositionaldataPlugin to gain a write-lock
		activePluginLock.unlock();

		this->selectActivePositionalDataPlugin();

		activePluginLock.relock();

		if (!this->activePositionalDataPlugin) {
			// It appears as if there is currently no plugin capable of delivering positional audio
			// Set positional data to zero-values
			this->positionalData.reset();

			return false;
		}
	}

	QWriteLocker posDataLock(&this->positionalData.lock);

	bool retStatus = this->activePositionalDataPlugin->fetchPositionalData(this->positionalData.playerPos, this->positionalData.playerDir,
		this->positionalData.playerAxis, this->positionalData.cameraPos, this->positionalData.cameraDir, this->positionalData.cameraAxis,
			this->positionalData.context, this->positionalData.identity);

	if (!retStatus) {
		// Shut the currently active plugin down and set a new one (if available)
		this->activePositionalDataPlugin->shutdownPositionalData();

		// unlock the read-lock in order to allow selectActivePositionaldataPlugin to gain a write-lock
		activePluginLock.unlock();

		this->selectActivePositionalDataPlugin();
	}

	return retStatus;

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
