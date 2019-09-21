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
#include <stdexcept>

class PluginError : public std::runtime_error {
	public:
		// inherit constructors of runtime_error
		using std::runtime_error::runtime_error;
};

class Plugin : public QObject {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(Plugin)
	protected:
		static QReadWriteLock idLock;
		static uint32_t nextID;

		Plugin(QString path, QObject *p = NULL);

		bool pluginIsValid;
		QLibrary lib;
		QString pluginPath;
		uint32_t pluginID;
		bool pluginIsLoaded;
		QReadWriteLock pluginLock;

		virtual bool doInitialize();
		virtual void resolveFunctionPointers();
		virtual void setFunctionPointersToNull();
		virtual void setDefaultImplementations();

	public:
		virtual ~Plugin() Q_DECL_OVERRIDE;
		virtual bool isValid();
		virtual bool isLoaded() Q_DECL_FINAL;
		virtual MumbleError_t load();
		virtual void unload();

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

		// -------- Function pointers to the library functions --------
		// For their documentation see the Plugin.h (the one used for creating plugins, not this file)
		
		// Functions every plugin must provide
	protected:
		// have those two protected in order to be able to track the plugin's status by providing custom functions for this fuctionality
		MumbleError_t (*init)();
		void          (*shutdown)();
	public:
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
		uint8_t       (*initPositionalData)(const char **programNames, const uint64_t **programPIDs, size_t programCount);
		bool          (*fetchPositionalData)(float *avatar_pos, float *avatar_front, float *avatar_axis, float *camera_pos, float *camera_front,
											float *camera_axis, const char **context, const char **identity);
		void          (*shutDownPositionalData)();
		
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

#endif
