// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGINMANAGER_H_
#define MUMBLE_MUMBLE_PLUGINMANAGER_H_

#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QString>
#include <QtCore/QHash>
#ifdef Q_OS_WIN
	#include <windows.h>
#endif
#include "Plugin.h"
#include "MumbleApplication.h"
#include "PositionalData.h"

class Global;

// Figure out where the plugin directories will be on the respective system
#ifdef QT_NO_DEBUG
	#ifndef PLUGIN_PATH
		#ifdef Q_OS_MAC
			#define PLUGIN_SYS_PATH QString::fromLatin1("%1/../Plugins").arg(qApp->applicationDirPath())
		#else // Q_OS_MAC
			#define PLUGIN_SYS_PATH QString::fromLatin1("%1/plugins").arg(MumbleApplication::instance()->applicationVersionRootPath())
		#endif // Q_OS_MAC
	#else // PLUGIN_PATH
		#define PLUGIN_PATH QLatin1String(MUMTEXT(PLUGIN_PATH))
	#endif // PLUGIN_PATH

	#define PLUGIN_USER_PATH (*Global::g_global_struct).qdBasePath.absolutePath() + QLatin1String("/Plugins")
#else // QT_NO_DEBUG
	#define PLUGIN_SYS_PATH QString::fromLatin1("%1/plugins").arg(MumbleApplication::instance()->applicationVersionRootPath())
	#define PLUGIN_USER_PATH QString()
#endif // QT_NO_DEBUG

class PluginManager : public QObject {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PluginManager)
	protected:
		mutable QReadWriteLock pluginCollectionLock;
		QHash<uint32_t, QSharedPointer<Plugin>> pluginHashMap;
		QString systemPluginsPath;
		QString userPluginsPath;
#ifdef Q_OS_WIN
		HANDLE hToken;
		TOKEN_PRIVILEGES tpPrevious;
		DWORD cbPrevious;
#endif
		PositionalData positionalData;

		mutable QReadWriteLock activePosDataPluginLock;
		QSharedPointer<Plugin> activePositionalDataPlugin;

		void clearPlugins();
		bool selectActivePositionalDataPlugin();
	public:
		PluginManager(QString sysPath = PLUGIN_SYS_PATH, QString userPath = PLUGIN_USER_PATH, QObject *p = NULL);
		virtual ~PluginManager() Q_DECL_OVERRIDE;

		const QSharedPointer<const Plugin> getPlugin(uint32_t pluginID) const;
		void checkForPluginUpdates() const;
		bool fetchPositionalData();
		void unlinkPositionalData();
		bool isPositionalDataAvailable() const;
		const PositionalData& getPositionalData() const;
		void enablePositionalDataFor(uint32_t pluginID, bool enable = true) const;
		const QVector<QSharedPointer<const Plugin> > getPlugins(bool sorted = false) const;

		// functions for accessing the PA context. The context itself is not publicly exposed in order to guarantee
		// that it is only accessed while holding a lock for it
	public slots:
		void rescanPlugins();
};

#endif
