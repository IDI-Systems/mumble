// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGIN_H_
#define MUMBLE_MUMBLE_PLUGIN_H_

#include "PluginComponents.h"
#include "PositionalData.h"
#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QString>
#include <QtCore/QLibrary>
#include <QtCore/QMutex>
#include <stdexcept>

/// A struct for holding the function pointers to the functions inside the plugin's library
/// For the documentation of those functions, see the plugin's header file (the one used when developing a plugin)
struct PluginAPIFunctions {
		MumbleError_t (PLUGIN_CALLING_CONVENTION *init)();
		void          (PLUGIN_CALLING_CONVENTION *shutdown)();
		const char*   (PLUGIN_CALLING_CONVENTION *getName)();
		Version_t     (PLUGIN_CALLING_CONVENTION *getAPIVersion)();
		void          (PLUGIN_CALLING_CONVENTION *registerAPIFunctions)(const MumbleAPI *api);

		// Further utility functions the plugin may implement
		void          (PLUGIN_CALLING_CONVENTION *setMumbleInfo)(Version_t mumbleVersion, Version_t mumbleAPIVersion, Version_t minimalExpectedAPIVersion);
		Version_t     (PLUGIN_CALLING_CONVENTION *getVersion)();
		const char*   (PLUGIN_CALLING_CONVENTION *getAuthor)();
		const char*   (PLUGIN_CALLING_CONVENTION *getDescription)();
		void          (PLUGIN_CALLING_CONVENTION *registerPluginID)(uint32_t id);
		uint32_t      (PLUGIN_CALLING_CONVENTION *getPluginFeatures)();
		uint32_t      (PLUGIN_CALLING_CONVENTION *deactivateFeatures)(uint32_t features);

		// Functions for dealing with positional audio (or rather the fetching of the needed data)
		uint8_t       (PLUGIN_CALLING_CONVENTION *initPositionalData)(const char **programNames, const uint64_t *programPIDs, size_t programCount);
		bool          (PLUGIN_CALLING_CONVENTION *fetchPositionalData)(float *avatarPos, float *avatarDir, float *avatarAxis, float *cameraPos, float *cameraDir,
											float *cameraAxis, const char **context, const char **identity);
		void          (PLUGIN_CALLING_CONVENTION *shutdownPositionalData)();
		
		// Callback functions and EventHandlers
		void          (PLUGIN_CALLING_CONVENTION *onServerConnected)(MumbleConnection_t connection);
		void          (PLUGIN_CALLING_CONVENTION *onServerDisconnected)(MumbleConnection_t connection);
		void          (PLUGIN_CALLING_CONVENTION *onChannelEntered)(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t previousChannelID, MumbleChannelID_t newChannelID);
		void          (PLUGIN_CALLING_CONVENTION *onChannelExited)(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t channelID);
		void          (PLUGIN_CALLING_CONVENTION *onUserTalkingStateChanged)(MumbleConnection_t connection, MumbleUserID_t userID, TalkingState_t talkingState);
		bool          (PLUGIN_CALLING_CONVENTION *onReceiveData)(MumbleConnection_t connection, MumbleUserID_t sender, const char *data, size_t dataLength, const char *dataID);
		bool          (PLUGIN_CALLING_CONVENTION *onAudioInput)(short *inputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech);
		bool          (PLUGIN_CALLING_CONVENTION *onAudioSourceFetched)(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);
		bool          (PLUGIN_CALLING_CONVENTION *onAudioSourceProcessed)(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);
		bool          (PLUGIN_CALLING_CONVENTION *onAudioOutputAboutToPlay)(float *outputPCM, uint32_t sampleCount, uint16_t channelCount);
};


/// An exception that is being thrown by a plugin whenever it encounters an error
class PluginError : public std::runtime_error {
	public:
		// inherit constructors of runtime_error
		using std::runtime_error::runtime_error;
};


/// An implementation similar to QReadLocker except that this one allows to lock on a lock the same thread is already
/// holding a write-lock on. This could also result in obtaining a write-lock though so it shouldn't be used for code regions
/// that take quite some time and rely on other readers still having access to the locked object.
class PluginReadLocker {
	protected:
		QReadWriteLock *lock;
	public:
		/// Constructor of the PluginReadLocker. If the passed lock-pointer is not nullptr, the constructor will
		/// already lock the provided lock.
		///
		/// @param lock A pointer to the QReadWriteLock that shall be managed by this object. May be nullptr
		PluginReadLocker(QReadWriteLock *lock);
		/// Locks this lock again after it has been unlocked before (Locking a locked lock results in a runtime error)
		void relock();
		/// Unlocks this lock
		void unlock();
		~PluginReadLocker();
};

/// A class representing a plugin library attached to Mumble. It can be used to manage (load/unload) and access plugin libraries.
class Plugin : public QObject {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(Plugin)
	protected:
		/// A mutex guarding Plugin::nextID
		static QMutex idLock;
		/// The ID of the plugin that will be loaded next. Whenever accessing this field, Plugin::idLock should be locked.
		static uint32_t nextID;

		/// Constructor of the Plugin.
		///
		/// @param path The path to the plugin's shared library file. This path has to exist unless isBuiltIn is true
		/// @param isBuiltIn A flag indicating that this is a plugin built into Mumble itself and is does not backed by a shared library
		/// @param p A pointer to a QObject representing the parent of this object or nullptr if there is no parent
		Plugin(QString path, bool isBuiltIn = false, QObject *p = nullptr);

		/// A flag indicating whether this plugin is valid. It is mainly used throughout the plugin's initialization.
		bool pluginIsValid;
		/// The QLibrary representing the shared library of this plugin
		QLibrary lib;
		/// The path to the shared library file in the host's filesystem
		QString pluginPath;
		/// The unique ID of this plugin. Note though that this ID is not suitable for uniquely identifying this plugin between restarts of Mumble
		/// (not even between rescans of the plugins) let alone across clients.
		uint32_t pluginID;
		// a flag indicating whether this plugin has been loaded by calling its init function.
		bool pluginIsLoaded;
		/// The lock guarding this plugin object. Every time a member is accessed this lock should be locked accordingly.
		mutable QReadWriteLock pluginLock;
		/// The struct holding the function pointers to the functions in the shared library.
		PluginAPIFunctions apiFnc;
		/// A flag indicating whether this plugin is built into Mumble and is thus not represented by a shared library.
		bool isBuiltIn;
		/// A flag indicating whether positional data gathering is enabled for this plugin (Enabled as in allowed via preferences).
		bool positionalDataIsEnabled;
		/// A flag indicating whether positional data gathering is currently active (Active as in running)
		bool positionalDataIsActive;

		/// Initializes this plugin. This function must be called directly after construction. This is guaranteed when the
		/// plugin is created via Plugin::createNew
		virtual bool doInitialize();
		/// Resolves the function pointers in the shared library and sets the respective fields in Plugin::apiFnc
		virtual void resolveFunctionPointers();
		/// Tells the plugin backend about its ID
		virtual void registerPluginID();

	public:
		virtual ~Plugin() Q_DECL_OVERRIDE;
		/// @returns Whether this plugin is in a valid state
		virtual bool isValid() const;
		/// @returns Whether this plugin is loaded (has been initialized via Plugin::init())
		virtual bool isLoaded() const Q_DECL_FINAL;
		/// @returns The unique ID of this plugin. This ID holds only as long as this plugin isn't "reconstructed".
		virtual uint32_t getID() const Q_DECL_FINAL;
		/// @returns Whether this plugin is built into Mumble (thus not backed by a shared library)
		virtual bool isBuiltInPlugin() const Q_DECL_FINAL;
		/// @returns The path to the shared library in the host's filesystem
		virtual QString getFilePath() const;
		/// @returns Whether positional data gathering is enabled (as in allowed via preferences)
		virtual bool isPositionalDataEnabled() const Q_DECL_FINAL;
		/// Enables positional data gathering for this plugin (as in allowing)
		///
		/// @param enable Whether to enable the data gathering
		virtual void enablePositionalData(bool enable = true);
		/// @returns Whether positional data gathering is currently active (as in running)
		virtual bool isPositionalDataActive() const Q_DECL_FINAL;

		/// A template function for instantiating new plugin objects and initializing them. The plugin will be allocated on the heap and has
		/// thus to be deleted via the delete instruction.
		///
		/// @tparam T The type of the plugin to be instantiated
		/// @tparam Ts The types of the contructor arguments
		/// @param args A list of args passed to the contructor of the plugin object
		/// @returns A pointer to the allocated plugin
		template<typename T, typename ... Ts>
		static T* createNew(Ts&&...args) {
			static_assert(std::is_base_of<Plugin, T>::value, "The Plugin::create() can only be used to instantiate objects of base-type Plugin");
			static_assert(!std::is_pointer<T>::value, "Plugin::create() can't be used to instantiate pointers. It will return a pointer automatically");

			T *instancePtr = new T(std::forward<Ts>(args)...);

			// call the initialize-method and throw an exception of it doesn't succeed
			if (!instancePtr->doInitialize()) {
				delete instancePtr;
				// Delete the constructed object to prevent a memory leak
				throw PluginError("Failed to initialize plugin");
			}

			return instancePtr;
		}

		/// Initializes this plugin
		virtual MumbleError_t init();
		/// Shuts this plugin down
		virtual void shutdown();
		/// @returns The name of this plugin
		virtual QString getName() const;
		/// @returns The API version this plugin intends to use
		virtual Version_t getAPIVersion() const;
		/// Delegates the struct of API function pointers to the plugin backend
		virtual void registerAPIFunctions(const MumbleAPI *api);

		/// Provides the plugin backend with some version information about Mumble
		///
		/// @param mumbleVersion The version of the Mumble client
		/// @param mumbleAPIVersion The API version used by the Mumble client
		/// @param minimalExpectedAPIVersion The minimal API version expected to be used by the plugin backend
		virtual void setMumbleInfo(Version_t mumbleVersion, Version_t mumbleAPIVersion, Version_t minimalExpectedAPIVersion);
		/// @returns The version of this plugin
		virtual Version_t getVersion() const;
		/// @returns The author of this plugin
		virtual QString getAuthor() const;
		/// @returns The plugin's description
		virtual QString getDescription() const;
		/// @returns The plugin's features or'ed together (See the PluginFeature enum in MumblePlugin.h for what features are available)
		virtual uint32_t getPluginFeatures() const;
		/// Asks the plugin to deactivate certain features
		///
		/// @param features The feature list or'ed together
		/// @returns The list of features that couldn't be deactivated or'ed together
		virtual uint32_t deactivateFeatures(uint32_t features);
		/// Shows an about-dialog
		///
		/// @parent A pointer to the QWidget that should be used as a parent
		/// @returns Whether the dialog could be shown successfully
		virtual bool showAboutDialog(QWidget *parent) const;
		/// Shows a config-dialog
		///
		/// @parent A pointer to the QWidget that should be used as a parent
		/// @returns Whether the dialog could be shown successfully
		virtual bool showConfigDialog(QWidget *parent) const;
		/// Initializes the positional data gathering
		///
		/// @params programNames A pointer to an array of const char* representing the program names
		/// @params programCount A pointer to an array of PIDs corresponding to the program names
		/// @params programCount The length of the two previous arrays
		virtual uint8_t initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount);
		/// Fetches the positional data
		///
		/// @param[out] avatarPos The position of the ingame avatar (player)
		/// @param[out] avatarDir The directiion in which the avatar (player) is looking/facing
		/// @param[out] avatarAxis The vector from the avatar's toes to its head
		/// @param[out] cameraPos The position of the ingame camera
		/// @param[out] cameraDir The direction in which the camera is looking/facing
		/// @param[out] cameraAxis The vector from the camera's bottom to its top
		/// @param[out] context The context of the current game-session (includes server/squad info)
		/// @param[out] identity The ingame identity of the player (name)
		virtual bool fetchPositionalData(Position3D& avatarPos, Vector3D& avatarDir, Vector3D& avatarAxis, Position3D& cameraPos, Vector3D& cameraDir,
				Vector3D& cameraAxis, QString& context, QString& identity);
		/// Shuts down positional data gathering
		virtual void shutdownPositionalData();
		/// Called to indicate that the client has connected to a server
		///
		/// @param connection An object used to identify the current connection
		virtual void onServerConnected(MumbleConnection_t connection);
		/// Called to indicate that the client disconnected from a server
		///
		/// @param connection An object used to identify the connection that has been disconnected
		virtual void onServerDisconnected(MumbleConnection_t connection);
		/// Called to indicate that a user has switched its channel
		///
		/// @param connection An object used to identify the current connection
		/// @param userID The ID of the user that switched channel
		/// @param previousChannelID The ID of the channel the user came from (-1 if there is no previous channel)
		/// æparam newChannelID The ID of the channel the user has switched to
		virtual void onChannelEntered(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t previousChannelID,
				MumbleChannelID_t newChannelID);
		/// Called to indicate that a user exited a channel.
		///
		/// @param connection An object used to identify the current connection
		/// @param userID The ID of the user that switched channel
		/// @param channelID The ID of the channel the user exited
		virtual void onChannelExited(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t channelID);
		/// Called to indicate that a user has changed its talking state
		///
		/// @param connection An object used to identify the current connection
		/// @param userID The ID of the user that switched channel
		/// @param talkingState The new talking state of the user
		virtual void onUserTalkingStateChanged(MumbleConnection_t connection, MumbleUserID_t userID, TalkingState_t talkingState);
		/// Called to indicate that a data packet has been received
		///
		/// @param connection An object used to identify the current connection
		/// @param sender The ID of the user whose client sent the data
		/// @param data The actual data
		/// @param dataLength The length of the data array
		/// @param datID The ID of the data used to determine whether this plugin handles this data or not
		/// @returns Whether this plugin handled the data
		virtual bool onReceiveData(MumbleConnection_t connection, MumbleUserID_t sender, const char *data, size_t dataLength, const char *dataID);
		/// Called to indicate that there is audio input
		///
		/// @param inputPCM A pointer to a short array representing the input PCM
		/// @param sampleCount The amount of samples per channel
		/// @param channelCount The amount of channels in the PCM
		/// @param isSpeech Whether Mumble considers the input as speech
		/// @returns Whether this pluign has modified the audio
		virtual bool onAudioInput(short *inputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech);
		/// Called to indicate that an audio source has been fetched
		///
		/// @param outputPCM A pointer to a short array representing the output PCM
		/// @param sampleCount The amount of samples per channel
		/// @param channelCount The amount of channels in the PCM
		/// @param isSpeech Whether Mumble considers the output as speech
		/// @param userID The ID of the user responsible for the output (only relevant if isSpeech == true)
		/// @returns Whether this pluign has modified the audio
		virtual bool onAudioSourceFetched(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);
		/// Called to indicate that the audio from a source has been processed
		///
		/// @param outputPCM A pointer to a short array representing the output PCM
		/// @param sampleCount The amount of samples per channel
		/// @param channelCount The amount of channels in the PCM
		/// @param isSpeech Whether Mumble considers the output as speech
		/// @param userID The ID of the user responsible for the output (only relevant if isSpeech == true)
		/// @returns Whether this pluign has modified the audio
		virtual bool onAudioSourceProcessed(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);
		/// Called to indicate that audio is about to be played
		///
		/// @param outputPCM A pointer to a short array representing the output PCM
		/// @param sampleCount The amount of samples per channel
		/// @param channelCount The amount of channels in the PCM
		/// @returns Whether this pluign has modified the audio
		virtual bool onAudioOutputAboutToPlay(float *outputPCM, uint32_t sampleCount, uint16_t channelCount);

		/// @returns Whether this plugin provides an about-dialog
		virtual bool providesAboutDialog() const;
		/// @returns Whether this plugin provides an config-dialog
		virtual bool providesConfigDialog() const;
};

#endif
