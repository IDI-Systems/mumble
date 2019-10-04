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
		bool          (PLUGIN_CALLING_CONVENTION *onAudioOutputAboutToPlay)(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech);
};


class PluginError : public std::runtime_error {
	public:
		// inherit constructors of runtime_error
		using std::runtime_error::runtime_error;
};

class PluginReadLocker {
	protected:
		QReadWriteLock *lock;
	public:
		PluginReadLocker(QReadWriteLock *lock);
		void relock();
		void unlock();
		~PluginReadLocker();
};

class Plugin : public QObject {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(Plugin)
	protected:
		static QMutex idLock;
		static uint32_t nextID;

		Plugin(QString path, bool isBuiltIn = false, QObject *p = nullptr);

		bool pluginIsValid;
		QLibrary lib;
		QString pluginPath;
		uint32_t pluginID;
		bool pluginIsLoaded;
		mutable QReadWriteLock pluginLock;
		PluginAPIFunctions apiFnc;
		bool isBuiltIn;
		bool positionalDataIsEnabled;
		bool positionalDataIsActive;

		virtual bool doInitialize();
		virtual void resolveFunctionPointers();

	public:
		virtual ~Plugin() Q_DECL_OVERRIDE;
		virtual bool isValid() const;
		virtual bool isLoaded() const Q_DECL_FINAL;
		virtual uint32_t getID() const Q_DECL_FINAL;
		virtual bool isBuiltInPlugin() const Q_DECL_FINAL;
		virtual QString getFilePath() const;
		virtual bool isPositionalDataEnabled() const Q_DECL_FINAL;
		virtual void enablePositionalData(bool enable = true);

		// template for a factory-method which is needed to ensure that every Plugin object will always
		// be initialized be the right call to its init() functions (if overwritten by a child-class, then
		// that version needs to be called)
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

		// functions for direct plugin-interaction
		virtual MumbleError_t init();
		virtual void shutdown();
		virtual QString getName() const;
		virtual Version_t getAPIVersion() const;
		virtual void registerAPIFunctions(const MumbleAPI *api);

		virtual void setMumbleInfo(Version_t mumbleVersion, Version_t mumbleAPIVersion, Version_t minimalExpectedAPIVersion);
		virtual Version_t getVersion() const;
		virtual QString getAuthor() const;
		virtual QString getDescription() const;
		virtual void registerPluginID(uint32_t id);
		virtual uint32_t getPluginFeatures() const;
		virtual uint32_t deactivateFeatures(uint32_t features);
		virtual bool showAboutDialog(QWidget *parent) const;
		virtual bool showConfigDialog(QWidget *parent) const;
		virtual uint8_t initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount);
		virtual bool fetchPositionalData(Position3D& avatarPos, Vector3D& avatarDir, Vector3D& avatarAxis, Position3D& cameraPos, Vector3D& cameraDir,
				Vector3D& cameraAxis, QString& context, QString& identity);
		virtual void shutdownPositionalData();
		virtual void onServerConnected(MumbleConnection_t connection);
		virtual void onServerDisconnected(MumbleConnection_t connection);
		virtual void onChannelEntered(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t previousChannelID,
				MumbleChannelID_t newChannelID);
		virtual void onChannelExited(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t channelID);
		virtual void onUserTalkingStateChanged(MumbleConnection_t connection, MumbleUserID_t userID, TalkingState_t talkingState);
		virtual bool onReceiveData(MumbleConnection_t connection, MumbleUserID_t sender, const char *data, size_t dataLength, const char *dataID);
		virtual bool onAudioInput(short *inputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech);
		virtual bool onAudioSourceFetched(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);
		virtual bool onAudioSourceProcessed(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);
		virtual bool onAudioOutputAboutToPlay(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech);

		// functions for checking which underlying plugin functions are implemented
		virtual bool providesAboutDialog() const;
		virtual bool providesConfigDialog() const;
};

#endif
