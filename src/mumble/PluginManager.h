// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGINMANAGER_H_
#define MUMBLE_MUMBLE_PLUGINMANAGER_H_

#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QString>
#ifdef Q_OS_WIN
	#include <windows.h>
#endif
#include "Plugin.h"
#include "MumbleApplication.h"

// We define a global macro called 'g'. This can lead to issues when included code uses 'g' as a type or parameter name (like protobuf 3.7 does). As such, for now, we have to make this our last include.
#include "Global.h"


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

	#define PLUGIN_USER_PATH g.qdBasePath.absolutePath() + QLatin1String("/Plugins")
#else // QT_NO_DEBUG
	#define PLUGIN_SYS_PATH QString::fromLatin1("%1/plugins").arg(MumbleApplication::instance()->applicationVersionRootPath())
	#define PLUGIN_USER_PATH QString()
#endif // QT_NO_DEBUG


class PluginManager : public QObject {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PluginManager)
	protected:
		QReadWriteLock pluginListLock;
		QList<QSharedPointer<Plugin>> pluginList;
		QString systemPluginsPath;
		QString userPluginsPath;
#ifdef Q_OS_WIN
		HANDLE hToken;
		TOKEN_PRIVILEGES tpPrevious;
		DWORD cbPrevious;
#endif

		void clearPlugins();
	public:
		PluginManager(QString sysPath = PLUGIN_SYS_PATH, QString userPath = PLUGIN_USER_PATH, QObject *p = NULL);
		virtual ~PluginManager() Q_DECL_OVERRIDE;
	public slots:
		void rescanPlugins();
};

#endif
