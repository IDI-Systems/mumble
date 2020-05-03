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
#include <QtCore/QVector>
#include <QtCore/QByteArray>
#include <QtCore/QChar>
#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>

#include "ManualPlugin.h"
#include "Log.h"
#include "ProcessResolver.h"
#include "ServerHandler.h"
#include "MumbleAPI.h"

#ifdef Q_OS_WIN
	#include <tlhelp32.h>
	#include <string>
#endif

#ifdef Q_OS_LINUX
	#include <QtCore/QStringList>
#endif

// We define a global macro called 'g'. This can lead to issues when included code uses 'g' as a type or parameter name (like protobuf 3.7 does). As such, for now, we have to make this our last include.
#include "Global.h"

PluginManager::PluginManager(QString sysPath, QString userPath, QObject *p) : QObject(p), pluginCollectionLock(QReadWriteLock::Recursive),
	pluginHashMap(), systemPluginsPath(sysPath), userPluginsPath(userPath), positionalData(), sentDataMutex(), sentData(),
		activePosDataPluginLock(QReadWriteLock::Recursive), activePositionalDataPlugin() {
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

	// By making this the parent of the created timer, we don't have to delete it explicitly
	QTimer *serverSyncTimer = new QTimer(this);
	QObject::connect(serverSyncTimer, &QTimer::timeout, this, &PluginManager::on_syncPositionalData);
	serverSyncTimer->start(500);
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

bool PluginManager::selectActivePositionalDataPlugin() {
	QReadLocker pluginLock(&this->pluginCollectionLock);
	QWriteLocker activePluginLock(&this->activePosDataPluginLock);

	if (!g.s.bTransmitPosition) {
		// According to the settings the position shall not be transmitted meaning that we don't have to select any plugin
		// for positional data
		this->activePositionalDataPlugin = QSharedPointer<Plugin>();

		return false;
	}

	ProcessResolver procRes(true);

	QHash<uint32_t, QSharedPointer<Plugin>>::iterator it = this->pluginHashMap.begin();

	// We assume that there is only one (enabled) plugin for the currently played game so we don't have to remember
	// which plugin was active last
	while (it != this->pluginHashMap.end()) {
		QSharedPointer<Plugin> currentPlugin = it.value();

		if (currentPlugin->isPositionalDataEnabled() && currentPlugin->isLoaded()) {
			// The const_cast is okay as it only removes the constness of the pointer itself. Since it is passed by value
			// changes made to the pointer won't affect us anyways thus justifying the const_cast
			switch(currentPlugin->initPositionalData(const_cast<const char**>(procRes.getProcessNames().data()),
						procRes.getProcessPIDs().data(), procRes.amountOfProcesses())) {
				case PDEC_OK:
					// the plugin is ready to provide positional data
					this->activePositionalDataPlugin = currentPlugin;

					return true;

				case PDEC_ERROR_PERM:
					// the plugin encountered a permanent error -> disable it
					g.l->log(Log::Warning, QString::fromUtf8("Plugin ") + currentPlugin->getName() +
							QString::fromUtf8(" encountered a permanent error in positional data gathering"));

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
	{
		QWriteLocker lock(&this->pluginCollectionLock);

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
					this->pluginHashMap.insert(p->getID(), p);
				} catch(const PluginError& e) {
					Q_UNUSED(e);
					// If an exception is thrown, this library does not represent a proper plugin
					// Check if it might be a legacy plugin instead
					try {
						QSharedPointer<LegacyPlugin> lp(Plugin::createNew<LegacyPlugin>(currentInfo.absoluteFilePath()));
						
#ifdef MUMBLE_PLUGIN_DEBUG
						LOG_FOUND_LEGACY_PLUGIN(lp, currentInfo.absoluteFilePath());
#endif
						this->pluginHashMap.insert(lp->getID(), lp);
					} catch(const PluginError& e) {
						Q_UNUSED(e);
						
						g.l->log(Log::Warning, QString::fromUtf8("Non-plugin found in plugin directory: ") + currentInfo.absoluteFilePath());
					}
				}
			}
		}

		// handle built-in plugins
#ifdef USE_MANUAL_PLUGIN
		try {
			QSharedPointer<ManualPlugin> mp(Plugin::createNew<ManualPlugin>());

			this->pluginHashMap.insert(mp->getID(), mp);
#ifdef MUMBLE_PLUGIN_DEBUG
			LOG_FOUND_BUILTIN(mp);
#endif
		} catch(const PluginError& e) {
			g.l->log(Log::Warning, QString::fromUtf8("Failed at loading manual plugin: ") + QString::fromUtf8(e.what()));
		}
#endif
	}

	QReadLocker readLock(&this->pluginCollectionLock);

	// load plugins based on settings
	// iterate over all plugins that have saved settings
	QHash<QString, PluginSetting>::const_iterator it = g.s.qhPluginSettings.constBegin();
	while (it != g.s.qhPluginSettings.constEnd()) {
		// for this we need a way to get a plugin based on the filepath
		const QString pluginPath = it.key();
		const PluginSetting setting = it.value();

		// iterate over all loaded plugins to see if the current setting is applicable
		QHash<uint32_t, QSharedPointer<Plugin>>::iterator pluginIt = this->pluginHashMap.begin();
		while (pluginIt != this->pluginHashMap.end()) {
			QSharedPointer<Plugin> plugin = pluginIt.value();

			if (plugin->getFilePath() == pluginPath) {
				if (setting.enabled) {
					this->loadPlugin(plugin->getID());

					const uint32_t features = plugin->getFeatures();

					if (!setting.positionalDataEnabled && (features & FEATURE_POSITIONAL)) {
						// try to deactivate the feature if the setting says so
						plugin->deactivateFeatures(FEATURE_POSITIONAL);
					}
				}

				// positional data is a special feature that has to be enabled/disabled in the Plugin wrapper class
				// additionally to telling the plugin library that the feature shall be deactivated
				plugin->enablePositionalData(setting.positionalDataEnabled);

				break;
			}

			pluginIt++;
		}

		it++;
	}
}

const QSharedPointer<const Plugin> PluginManager::getPlugin(uint32_t pluginID) const {
	QReadLocker lock(&this->pluginCollectionLock);
	
	return this->pluginHashMap.value(pluginID);
}

void PluginManager::checkForPluginUpdates() const {
	// TODO
}

bool PluginManager::fetchPositionalData() {
	if (g.bPosTest) {
		// This is for testing-purposes only so the "fetched" position doesn't have any real meaning
		this->positionalData.reset();

		this->positionalData.playerDir.z = 1.0f;
		this->positionalData.playerAxis.y = 1.0f;
		this->positionalData.cameraDir.z = 1.0f;
		this->positionalData.cameraAxis.y = 1.0f;

		return true;
	}

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

	// Add the plugin's name to the context as well to prevent name-clashes between plugins
	if (!positionalData.context.isEmpty()) {
		this->positionalData.context = this->activePositionalDataPlugin->getName() + QChar::Null + this->positionalData.context;
	}

	if (!retStatus) {
		// Shut the currently active plugin down and set a new one (if available)
		this->activePositionalDataPlugin->shutdownPositionalData();

		// unlock the read-lock in order to allow selectActivePositionaldataPlugin to gain a write-lock
		activePluginLock.unlock();

		this->selectActivePositionalDataPlugin();
	}

	return retStatus;
}

void PluginManager::unlinkPositionalData() {
	QWriteLocker lock(&this->activePosDataPluginLock);

	if (this->activePositionalDataPlugin) {
		// Shut the plugin down
		this->activePositionalDataPlugin->shutdown();

		// Set the pointer to NULL
		this->activePositionalDataPlugin = QSharedPointer<Plugin>();
	}
}

bool PluginManager::isPositionalDataAvailable() const {
	QReadLocker lock(&this->activePosDataPluginLock);

	return this->activePositionalDataPlugin != nullptr;
}

const PositionalData& PluginManager::getPositionalData() const {
	return this->positionalData;
}

void PluginManager::enablePositionalDataFor(uint32_t pluginID, bool enable) const {
	QReadLocker lock(&this->pluginCollectionLock);

	QSharedPointer<Plugin> plugin = this->pluginHashMap.value(pluginID);

	if (plugin) {
		plugin->enablePositionalData(enable);
	}
}

const QVector<QSharedPointer<const Plugin> > PluginManager::getPlugins(bool sorted) const {
	QReadLocker lock(&this->pluginCollectionLock);

	QVector<QSharedPointer<const Plugin>> pluginList;

	QHash<uint32_t, QSharedPointer<Plugin>>::const_iterator it = this->pluginHashMap.constBegin();
	if (sorted) {
		QList<uint32_t> ids = this->pluginHashMap.keys();

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

bool PluginManager::loadPlugin(uint32_t pluginID) const {
	QReadLocker lock(&this->pluginCollectionLock);

	QSharedPointer<Plugin> plugin = pluginHashMap.value(pluginID);

	if (plugin) {
		if (plugin->init() == STATUS_OK) {
			try {
				MumbleAPI api = API::getMumbleAPI(plugin->getAPIVersion());

				plugin->registerAPIFunctions(api);

				return true;
			} catch (const std::invalid_argument& e) {
				// The API version could not be obtained -> this is an invalid plugin that shouldn't have been loaded in the first place
#ifdef QT_DEBUG
				qCritical() << e.what();
#else
				Q_UNUSED(e);
#endif

				plugin->shutdown();
			}
		}
	}

	return false;
}

void PluginManager::unloadPlugin(uint32_t pluginID) const {
	QReadLocker lock(&this->pluginCollectionLock);
	
	QSharedPointer<Plugin> plugin = pluginHashMap.value(pluginID);

	if (plugin) {
		plugin->shutdown();
	}
}

uint32_t  PluginManager::deactivateFeaturesFor(uint32_t pluginID, uint32_t features) const {
	QReadLocker lock(&this->pluginCollectionLock);

	QSharedPointer<Plugin> plugin = pluginHashMap.value(pluginID);

	if (plugin) {
		return plugin->deactivateFeatures(features);
	}

	return FEATURE_NONE;
}

void PluginManager::foreachPlugin(std::function<void(Plugin&)> pluginProcessor) const {
	QReadLocker lock(&this->pluginCollectionLock);

	QHash<uint32_t, QSharedPointer<Plugin>>::const_iterator it = this->pluginHashMap.constBegin();

	while (it != this->pluginHashMap.constEnd()) {
		pluginProcessor(*it.value());

		it++;
	}
}

void PluginManager::on_serverConnected() const {
	const MumbleConnection_t connectionID = g.sh->getConnectionID();

#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug("PluginManager: Connected to a server with connection ID %d", connectionID);
#endif

	this->foreachPlugin([connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onServerConnected(connectionID);
		}
	});
}

void PluginManager::on_serverDisconnected() const {
	const MumbleConnection_t connectionID = g.sh->getConnectionID();

#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug("PluginManager: Disconnected from a server with connection ID %d", connectionID);
#endif

	this->foreachPlugin([connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onServerDisconnected(connectionID);
		}
	});
}

void PluginManager::on_channelEntered(const Channel *newChannel, const Channel *prevChannel, const User *user) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: User" << user->qsName <<  "entered channel" << newChannel->qsName << "- ID:" << newChannel->iId;
#endif

	if (!g.sh) {
		// if there is no server-handler, there is no (real) channel to enter
		return;
	}

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([user, newChannel, prevChannel, connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelEntered(connectionID, user->uiSession, prevChannel ? prevChannel->iId : -1, newChannel->iId);
		}
	});
}

void PluginManager::on_channelExited(const Channel *channel, const User *user) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: User" << user->qsName <<  "left channel" << channel->qsName << "- ID:" << channel->iId;
#endif

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([user, channel, connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelExited(connectionID, user->uiSession, channel->iId);
		}
	});
}

QString getTalkingStateStr(Settings::TalkState ts) {
	switch(ts) {
		case Settings::TalkState::Passive:
			return QString::fromUtf8("Passive");
		case Settings::TalkState::Talking:
			return QString::fromUtf8("Talking");
		case Settings::TalkState::Whispering:
			return QString::fromUtf8("Whispering");
		case Settings::TalkState::Shouting:
			return QString::fromUtf8("Shouting");
	}

	return QString::fromUtf8("Unknown");
}

void PluginManager::on_userTalkingStateChanged() const {
	const ClientUser *user = qobject_cast<ClientUser*>(QObject::sender());
#ifdef MUMBLE_PLUGIN_DEBUG
	if (user) {
		qDebug() << "PluginManager: User" << user->qsName << "changed talking state to" << getTalkingStateStr(user->tsState);
	} else {
		qCritical() << "PluginManager: Unable to identify ClientUser";
	}
#endif

	if (user) {
		// Convert Mumble's talking state to the TalkingState used in the API
		TalkingState_t ts;

		switch(user->tsState) {
			case Settings::TalkState::Passive:
				ts = PASSIVE;
				break;
			case Settings::TalkState::Talking:
				ts = TALKING;
				break;
			case Settings::TalkState::Whispering:
				ts = WHISPERING;
				break;
			case Settings::TalkState::Shouting:
				ts = SHOUTING;
				break;
			default:
				ts = INVALID;
		}

		if (ts == INVALID) {
			// An error occured
			return;
		}

		const MumbleConnection_t connectionID = g.sh->getConnectionID();

		foreachPlugin([user, ts, connectionID](Plugin& plugin) {
			if (plugin.isLoaded()) {
				plugin.onUserTalkingStateChanged(connectionID, user->uiSession, ts);
			}
		});
	}
}

void PluginManager::on_audioInput(short *inputPCM, unsigned int sampleCount, unsigned int channelCount, bool isSpeech) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: AudioInput with" << channelCount << "channels and" << sampleCount << "samples per channel. IsSpeech:" << isSpeech;
#endif

	this->foreachPlugin([inputPCM, sampleCount, channelCount, isSpeech](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onAudioInput(inputPCM, sampleCount, channelCount, isSpeech);
		}
	});
}

void PluginManager::on_audioSourceFetched(float *outputPCM, unsigned int sampleCount, unsigned int channelCount, bool isSpeech, const ClientUser *user) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: AudioSource with" << channelCount << "channels and" << sampleCount << "samples per channel fetched. IsSpeech:" << isSpeech
		<< "Sender-ID:" << user->uiSession;
#endif

	this->foreachPlugin([outputPCM, sampleCount, channelCount, isSpeech, user](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onAudioSourceFetched(outputPCM, sampleCount, channelCount, isSpeech, user ? user->uiSession : -1);
		}
	});
}

void PluginManager::on_audioOutputAboutToPlay(float *outputPCM, unsigned int sampleCount, unsigned int channelCount) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: AudioOutput with" << channelCount << "channels and" << sampleCount << "samples per channel";
#endif

	this->foreachPlugin([outputPCM, sampleCount, channelCount](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onAudioOutputAboutToPlay(outputPCM, sampleCount, channelCount);
		}
	});
}

void PluginManager::on_receiveData(const ClientUser *sender, const char *data, size_t dataLength, const char *dataID) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: Data with ID" << dataID << "and length" << dataLength << "received. Sender-ID:" << sender->uiSession;
#endif

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([sender, data, dataLength, dataID, connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onReceiveData(connectionID, sender->uiSession, data, dataLength, dataID);
		}
	});
}

void PluginManager::on_serverSynchronized() const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: Server synchronized";
#endif

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onServerSynchronized(connectionID);
		}
	});
}

void PluginManager::on_userAdded(MumbleUserID_t userID) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: Added user with ID" << userID;
#endif

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([userID, connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onUserAdded(connectionID, userID);
		};
	});
}

void PluginManager::on_userRemoved(MumbleUserID_t userID) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: Removed user with ID" << userID;
#endif

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([userID, connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onUserRemoved(connectionID, userID);
		};
	});
}

void PluginManager::on_channelAdded(MumbleChannelID_t channelID) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: Added channel with ID" << channel->channelID;
#endif

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([channelID, connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelAdded(connectionID, channelID);
		};
	});
}

void PluginManager::on_channelRemoved(MumbleChannelID_t channelID) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: Removed channel with ID" << channelID;
#endif

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([channelID, connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelRemoved(connectionID, channelID);
		};
	});
}

void PluginManager::on_channelRenamed(int channelID) const {
#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "PluginManager: Renamed channel with ID" << channelID;
#endif

	const MumbleConnection_t connectionID = g.sh->getConnectionID();

	this->foreachPlugin([channelID, connectionID](Plugin& plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelRenamed(connectionID, channelID);
		};
	});
}

void PluginManager::on_syncPositionalData() {
	// fetch positional data
	if (this->fetchPositionalData()) {
		// Sync the gathered data (context + identity) with the server
		if (!g.uiSession) {
			// For some reason the local session ID is not set -> clear all data sent to the server in order to gurantee
			// a re-send once the session is restored and there is data available
			QMutexLocker mLock(&this->sentDataMutex);

			this->sentData.context.clear();
			this->sentData.identity.clear();
		} else {
			// Check if the identity and/or the context has changed and if it did, send that new info to the server
			QMutexLocker mLock(&this->sentDataMutex);
			QReadLocker rLock(&this->positionalData.lock);

			if (this->sentData.context != this->positionalData.context || this->sentData.identity != this->positionalData.identity ) {
				MumbleProto::UserState mpus;
				mpus.set_session(g.uiSession);

				if (this->sentData.context != this->positionalData.context) {
					this->sentData.context = this->positionalData.context;
					mpus.set_plugin_context(this->sentData.context.toUtf8().constData());
				}
				if (this->sentData.identity != this->positionalData.identity) {
					this->sentData.identity = this->positionalData.identity;
					mpus.set_plugin_identity(this->sentData.identity.toUtf8().constData());
				}

				if (g.sh) {
					// send the message if the serverHandler is available
					g.sh->sendMessage(mpus);
				}
			}
		}
	}


}
