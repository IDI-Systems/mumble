// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGIN_H_
#define MUMBLE_MUMBLE_PLUGIN_H_

#include "PluginComponents.h"
#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QString>
#include <QtCore/QLibrary>
#include <QtCore/QMutex>
#include <stdexcept>

/// A struct for holding the function pointers to the functions inside the plugin's library
/// For the documentation of those functions, see the plugin's header file (the one used when developing a plugin)
struct PluginAPIFunctions {
		MumbleError_t (*init)();
		void          (*shutdown)();
		const char*   (*getName)();
		Version_t     (*getAPIVersion)();
		void          (*registerAPIFunctions)(const MumbleAPI *api);

		// Further utility functions the plugin may implement
		void          (*setMumbleInfo)(Version_t mumbleVersion, Version_t mumbleAPIVersion, Version_t minimalExpectedAPIVersion);
		Version_t     (*getVersion)();
		const char*   (*getAuthor)();
		const char*   (*getDescription)();
		void          (*registerPluginID)(uint32_t id);
		uint32_t      (*getPluginFeatures)();
		uint32_t      (*deactivateFeatures)(uint32_t features);

		// Functions for dealing with positional audio (or rather the fetching of the needed data)
		uint8_t       (*initPositionalData)(const char **programNames, const uint64_t *programPIDs, size_t programCount);
		bool          (*fetchPositionalData)(float *avatar_pos, float *avatar_front, float *avatar_axis, float *camera_pos, float *camera_front,
											float *camera_axis, const char **context, const char **identity);
		void          (*shutdownPositionalData)();
		
		// Callback functions and EventHandlers
		void          (*onServerConnected)(MumbleConnection_t connection);
		void          (*onServerDisconnected)(MumbleConnection_t connection);
		void          (*onChannelEntered)(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t previousChannelID, MumbleChannelID_t newChannelID);
		void          (*onChannelExited)(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t channelID);
		void          (*onUserTalkingStateChanged)(MumbleConnection_t connection, MumbleUserID_t userID, TalkingState_t talkingState);
		bool          (*onReceiveData)(MumbleConnection_t connection, MumbleUserID_t sender, const char *data, size_t dataLength, const char *dataID);
		bool          (*onAudioInput)(short *inputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech);
		bool          (*onAudioSourceFetched)(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);
		bool          (*onAudioSourceProcessed)(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);
		bool          (*onAudioOutputAboutToPlay)(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech);
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
		~PluginReadLocker();
};

class Plugin : public QObject {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(Plugin)
	protected:
		static QMutex idLock;
		static uint32_t nextID;

		Plugin(QString path, QObject *p = nullptr);

		bool pluginIsValid;
		QLibrary lib;
		QString pluginPath;
		uint32_t pluginID;
		bool pluginIsLoaded;
		mutable QReadWriteLock pluginLock;
		PluginAPIFunctions apiFnc;

		virtual bool doInitialize();
		virtual void resolveFunctionPointers();

	public:
		virtual ~Plugin() Q_DECL_OVERRIDE;
		virtual bool isValid() const;
		virtual bool isLoaded() const Q_DECL_FINAL;

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
		virtual const char* getName() const;
		virtual Version_t getAPIVersion() const;
		virtual void registerAPIFunctions(const MumbleAPI *api);

		virtual void setMumbleInfo(Version_t mumbleVersion, Version_t mumbleAPIVersion, Version_t minimalExpectedAPIVersion);
		virtual Version_t getVersion() const;
		virtual const char* getAuthor() const;
		virtual const char* getDescription() const;
		virtual void registerPluginID(uint32_t id);
		virtual uint32_t getPluginFeatures() const;
		virtual uint32_t deactivateFeatures(uint32_t features);
		virtual uint8_t initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount);
		virtual bool fetchPositionalData(float *avatar_pos, float *avatar_front, float *avatar_axis, float *camera_pos, float *camera_front,
				float *camera_axis, const char **context, const char **identity);
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
};

#endif
