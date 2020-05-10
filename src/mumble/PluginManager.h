// Copyright 2019-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGINMANAGER_H_
#define MUMBLE_MUMBLE_PLUGINMANAGER_H_

#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QString>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#ifdef Q_OS_WIN
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif
#include "Plugin.h"
#include "MumbleApplication.h"
#include "PositionalData.h"

#include "User.h"
#include "ClientUser.h"
#include "Channel.h"
#include "Settings.h"
#include "PluginUpdater.h"

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

	#define PLUGIN_USER_PATH ((*Global::g_global_struct).qdBasePath.absolutePath() + QLatin1String("/Plugins"))
#else // QT_NO_DEBUG
	#define PLUGIN_SYS_PATH QString::fromLatin1("%1/plugins").arg(MumbleApplication::instance()->applicationVersionRootPath())
	#define PLUGIN_USER_PATH QString()
#endif // QT_NO_DEBUG

// If the macro g has been defined by including Global.h from this header file it has to be undefined again in order to avoid
// name clashes with e.g. variable names in protobuf
#ifndef G_WAS_ALREADY_DEFINED
	#undef g
#endif

/// A struct for holding the values of the current context and identity that have been sent to the server
struct PluginManager_SentData {
	QString context;
	QString identity;
};


/// The plugin manager is the central object dealing with everything plugin-related. It is responsible for
/// finding, loading and managing the plugins. It also is responsible for invoking callback functions in the plugins
/// and can be used by Mumble to communicate with them
class PluginManager : public QObject {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PluginManager)
	protected:
		/// Lock for pluginHashMap. This lock has to be aquired when accessing pluginHashMap
		mutable QReadWriteLock m_pluginCollectionLock;
		/// A map between plugin-IDs and the actual plugin objects. You have to aquire pluginCollectionLock before
		/// accessing this map.
		QHash<plugin_id_t, plugin_ptr_t> m_pluginHashMap;
		/// The path to the system-directory containing plugins
		QString m_systemPluginsPath;
		/// The path to the user-directory containing plugins
		QString m_userPluginsPath;
#ifdef Q_OS_WIN
		// This stuff is apparently needed on Windows in order to deal with DLLs
		HANDLE m_hToken;
		TOKEN_PRIVILEGES m_tpPrevious;
		DWORD m_cbPrevious;
#endif
		/// The PositionalData object holding the current positional data (as retrieved by the respective plugin)
		PositionalData m_positionalData;

		/// The mutex for sentData. This has to be aquired before accessing sentData
		mutable QMutex m_sentDataMutex;
		/// The bits of the positional data that have already been sent to the server. It is used to determine whether
		/// the new data has to be sent to the server (in case it has changed). You have ti aquire sentDataMutex before
		/// accessing this field.
		PluginManager_SentData m_sentData;

		/// The lock for activePositionalDataPlugin. It has to be aquired before accessing the respective field.
		mutable QReadWriteLock m_activePosDataPluginLock;
		/// The plugin that is currently used to retrieve positional data. You have to aquire activePosDataPluginLock before
		/// accessing this field.
		plugin_ptr_t m_activePositionalDataPlugin;
		/// The PluginUpdater used to handle plugin updates.
		PluginUpdater m_updater;

		// We override the QObject::eventFilter function in order to be able to install the pluginManager as an event filter
		// to the main application in order to get notified about keystrokes.
		bool eventFilter(QObject *target, QEvent *event) Q_DECL_OVERRIDE;
		
		/// Unloads all plugins that are currently loaded.
		void unloadPlugins() const;
		/// Clears the current list of plugins
		void clearPlugins();
		/// Iterates over the plugins and tries to select a plugin that currently claims to be able to deliver positional data. If
		/// it found a plugin, activePositionalDataPlugin is set accordingly. If not, it is set to nullptr.
		///
		/// @returns Whether this function succeeded in finding such a plugin
		bool selectActivePositionalDataPlugin();

		/// A internal helper function that iterates over all plugins and calls the given function providing the current plugin as
		/// a parameter.
		void foreachPlugin(std::function<void(Plugin&)>) const;
	public:
		/// Constructor
		///
		/// @param sysPath The path to the system-directory containing plugins
		/// @param userPath The path to the user-directory containing plugins
		/// @param p The parent QObject
		PluginManager(QString sysPath = PLUGIN_SYS_PATH, QString userPath = PLUGIN_USER_PATH, QObject *p = NULL);
		/// Destructor
		virtual ~PluginManager() Q_DECL_OVERRIDE;

		/// @param pluginID The ID of the plugin that should be retreved
		/// @returns A pointer to the plugin with the given ID or nullptr if no such plugin could be found
		const_plugin_ptr_t getPlugin(plugin_id_t pluginID) const;
		/// Checks whether there are any updates for the plugins and if there are it invokes the PluginUpdater.
		void checkForPluginUpdates();
		/// Fetches positional data from the activePositionalDataPlugin if there is one set. This function will update the
		/// positionalData field
		///
		/// @returns Whether the positional data could be retrieved successfully
		bool fetchPositionalData();
		/// Unlinks the currently active positional data plugin. Effectively this sets activePositionalDataPlugin to nullptr
		void unlinkPositionalData();
		/// @returns Whether positional data is currently available (it has been successfully set via fetchPositionalData)
		bool isPositionalDataAvailable() const;
		/// @returns The most recent positional data
		const PositionalData& getPositionalData() const;
		/// Enables positional data gathering for the plugin with the given ID. A plugin is only even asked whether it can deliver
		/// positional data if this is enabled.
		///
		/// @param pluginID The ID of the plugin to access
		/// @param enable Whether to enable positional data (alternative is to disable it)
		void enablePositionalDataFor(plugin_id_t pluginID, bool enable = true) const;
		/// @returns A const vector of the plugins
		const QVector<const_plugin_ptr_t> getPlugins(bool sorted = false) const;
		/// Loads the plugin with the given ID. Loading means initializing the plugin.
		///
		/// @param pluginID The ID of the plugin to load
		/// @returns Whether the plugin could be successfully loaded
		bool loadPlugin(plugin_id_t pluginID) const;
		/// Unloads the plugin with the given ID. Unloading means shutting the pluign down.
		///
		/// @param pluginID The ID of the plugin to unload
		void unloadPlugin(plugin_id_t pluginID) const;
		/// Deactivates the given features for the plugin with the given ID
		///
		/// @param pluginID The ID of the plugin to access
		/// @param features The feature set that should be deactivated. The features are or'ed together.
		/// @returns The feature set that could not be deactivated
		uint32_t deactivateFeaturesFor(plugin_id_t pluginID, uint32_t features) const;
		/// Allows or forbids the given plugin to monitor keyboard events.
		///
		/// @param pluginID The ID of the plugin to access
		/// @param allow Whether to allow the monitoring or not
		void allowKeyboardMonitoringFor(plugin_id_t pluginID, bool allow) const;
		/// Checks whether a plugin with the given ID exists.
		///
		/// @param pluginID The ID to check
		/// @returns Whether such a plugin exists
		bool pluginExists(plugin_id_t pluginID) const;

	public slots:
		/// Rescans the plugin directory and load all plugins from there after having cleared the current plugin list
		void rescanPlugins();
		/// Slot that gets called whenever data from another plugin has been received. This function will then delegate
		/// this to the respective plugin callback
		///
		/// @param sender A pointer to the ClientUser whose client has sent the data
		/// @param data The byte-array representing the sent data
		/// @param dataLength The length of the data array
		/// @param dataID The ID of the data
		void on_receiveData(const ClientUser *sender, const char *data, size_t dataLength, const char *dataID) const;

	protected slots:
		/// Slot that gets called when the local client connects to a server. It will delegate it to the respective plugin callback.
		void on_serverConnected() const;
		/// Slot that gets called when the local client disconnects to a server. It will delegate it to the respective plugin callback.
		void on_serverDisconnected() const;
		/// Slot that gets called when a client enters a channel. It will delegate it to the respective plugin callback.
		///
		/// @param newChannel A pointer to the new channel
		/// @param prevChannel A pointer to the previous channel or nullptr if no such channel exists
		/// @param user A pointer to the user that entered the channel
		void on_channelEntered(const Channel *newChannel, const Channel *prevChannel, const User *user) const;
		/// Slot that gets called when a client leaves a channel. It will delegate it to the respective plugin callback.
		///
		/// @param channel A pointer to the channel that has been left
		/// @param user A pointer to the user that entered the channel
		void on_channelExited(const Channel *channel, const User *user) const;
		/// Slot that gets called when the local client changes its talking state. It will delegate it to the respective plugin callback.
		void on_userTalkingStateChanged() const;
		/// Slot that gets called when the local client receives audio input. It will delegate it to the respective plugin callback.
		///
		/// @param inputPCM The array containing the input PCM (pulse-code-modulation). Its length is sampleCount * channelCount
		/// @param sampleCount The amount of samples in the PCM array
		/// @param channelCount The amount of channels in the PCM array
		/// @param isSpeech Whether Mumble considers this input as speech
		void on_audioInput(short *inputPCM, unsigned int sampleCount, unsigned int channelCount, bool isSpeech) const;
		/// Slot that gets called when the local client has fetched an audio source. It will delegate it to the respective plugin callback.
		///
		/// @param outputPCM The array containing the output-PCM (pulse-code-modulation). Its length is sampleCount * channelCount
		/// @param sampleCount The amount of samples in the PCM array
		/// @param channelCount The amount of channels in the PCM array
		/// @param isSpeech Whether Mumble considers this input as speech
		/// @param user A pointer to the ClientUser the audio source corresposnds to
		void on_audioSourceFetched(float *outputPCM, unsigned int sampleCount, unsigned int channelCount, bool isSpeech, const ClientUser *user) const;
		/// Slot that gets called when the local client is about to play some audio. It will delegate it to the respective plugin callback.
		///
		/// @param outputPCM The array containing the output-PCM (pulse-code-modulation). Its length is sampleCount * channelCount
		/// @param sampleCount The amount of samples in the PCM array
		/// @param channelCount The amount of channels in the PCM array
		void on_audioOutputAboutToPlay(float *outputPCM, unsigned int sampleCount, unsigned int channelCount, bool *modifiedAudio) const;
		/// Slot that gets called after the local client has finished synchronizing with the server. It will delegate it to the respective
		/// plugin callback.
		void on_serverSynchronized() const;
		/// Slot that gets called when a new user is added to the user model. It will delegate it to the respective plugin callbacks.
		/// 
		/// @param userID The ID of the added user
		void on_userAdded(unsigned int userID) const;
		/// Slot that gets called when a user is removed from the user model. It will delegate it to the respective plugin callbacks.
		/// 
		/// @param userID The ID of the removed user
		void on_userRemoved(unsigned int userID) const;
		/// Slot that gets called when a new channel is added to the user model. It will delegate it to the respective plugin callbacks.
		/// 
		/// @param channelID The ID of the added channel
		void on_channelAdded(int channelID) const;
		/// Slot that gets called when a channel is removed from the user model. It will delegate it to the respective plugin callbacks.
		/// 
		/// @param channelID The ID of the removed channel
		void on_channelRemoved(int channelID) const;
		/// Slot that gets called when a channel is renamed. It will delegate it to the respective plugin callbacks.
		///
		/// @param channelID The ID of the renamed channel
		void on_channelRenamed(int channelID) const;
		/// Slot that gets called when a key has been pressed or released while Mumble has keyboard focus.
		///
		/// @param key The code of the affected key (as encoded by Qt::Key)
		/// @param modifiers The modifiers that were active in the moment of the event
		/// @param isPress True if the key has been pressed, false if it has been released
		void on_keyEvent(unsigned int key, Qt::KeyboardModifiers modifiers, bool isPress) const;

		/// Slot that gets called whenever the positional data should be synchronized with the server. Before it does that, it tries to
		/// fetch new data.
		void on_syncPositionalData();
		/// Slot called if there are plugin updates available
		void on_updatesAvailable();

	signals:
		/// A signal emitted if the PluginManager (acting as an event filter) detected
		/// a QKeyEvent.
		///
		/// @param key The code of the affected key (as encoded by Qt::Key)
		/// @param modifiers The modifiers that were active in the moment of the event
		/// @param isPress True if the key has been pressed, false if it has been released
		void keyEvent(unsigned int key, Qt::KeyboardModifiers modifiers, bool isPress);
};

#endif
