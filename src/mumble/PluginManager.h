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

#include "User.h"
#include "ClientUser.h"
#include "Channel.h"
#include "Settings.h"

#include <functional>

// Check whether the macro g has been defined before including Global.h
#ifdef g
	#define G_WAS_ALREADY_DEFINED
#endif
// Global.h defines a macro g that can lead to name clashes with e.g. variable names inside protobuf. Thus it should normally 
// be included last. This isn't possible here though so we establish a workaround so that we don't let the definition of g
// get outside the relevant section if it hasn't been defined before this include. This is possible because the macro definition
// is placed outside the header guard of Global.h allowing for a later macro definition on inclusion of Global.h
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

	#define PLUGIN_USER_PATH (*Global::g_global_struct).qdBasePath.absolutePath() + QLatin1String("/Plugins")
#else // QT_NO_DEBUG
	#define PLUGIN_SYS_PATH QString::fromLatin1("%1/plugins").arg(MumbleApplication::instance()->applicationVersionRootPath())
	#define PLUGIN_USER_PATH QString()
#endif // QT_NO_DEBUG

// If the macro g has been defined by including Global.h from this header file it has to be undefined again in order to avoid
// name clashes with e.g. variable names in protobuf
#ifndef G_WAS_ALREADY_DEFINED
	#undef g
#endif

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

		void foreachPlugin(std::function<void(Plugin&)>) const;
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

	public slots:
		void rescanPlugins();

	protected slots:
		void on_serverConnected() const;
		void on_serverDisconnected() const;
		void on_channelEntered(const Channel *channel, const User *user) const;
		void on_channelExited(const Channel *channel, const User *user) const;
		void on_userTalkingStateChanged() const;
		void on_audioInput(short *inputPCM, unsigned int sampleCount, unsigned int channelCount, bool isSpeech) const;
		void on_audioSourceFetched(float *outputPCM, unsigned int sampleCount, unsigned int channelCount, bool isSpeech, const ClientUser *user) const;
		void on_audioOutputAboutToPlay(float *outputPCM, unsigned int sampleCount, unsigned int channelCount) const;
		void on_receiveData(const ClientUser *sender, const char *data, size_t dataLength, const char *dataID) const;
};

#endif
