// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Plugin.h"
#include <QtCore/QWriteLocker>
#include <QtCore/QReadLocker>


// -------- Default implementations --------
void default_setMumbleInfo(Version_t mumbleVersion, Version_t mumbleAPIVersion, Version_t minimalExpectedAPIVersion) {
}

Version_t default_getVersion() {
	return {0, 0, 0};
}

const char* default_getAuthor() {
	return "Unknown";
}

const char* default_getDescription() {
	return "No description provided";
}

void default_registerPluginID(uint32_t id) {
}

uint32_t default_getPluginFeatures() {
	return FEATURE_NONE;
}

uint32_t default_deactivateFeatures(uint32_t features) {
	return features;
}

uint8_t default_initPositionalData(const char **programNames, const uint64_t **programPIDs, size_t programCount) {
	return PDEC_ERROR_PERM;
}

#define SET_3D_TO_ZERO(arrayName) arrayName[0] = 0; arrayName[1] = 0; arrayName[2] = 0
bool default_fetchPositionalData(float *avatar_pos, float *avatar_front, float *avatar_axis, float *camera_pos, float *camera_front,
		float *camera_axis, const char **context, const char **identity) {
	SET_3D_TO_ZERO(avatar_pos);
	SET_3D_TO_ZERO(avatar_front);
	SET_3D_TO_ZERO(avatar_axis);
	SET_3D_TO_ZERO(camera_pos);
	SET_3D_TO_ZERO(camera_front);
	SET_3D_TO_ZERO(camera_axis);
	context = nullptr;
	identity = nullptr;

	return false;
}
	
void default_shutDownPositionalData() {
}

void default_onServerConnected(MumbleConnection_t connection) {
}

void default_onServerDisconnected(MumbleConnection_t connection) {
}

void default_onChannelEntered(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t previousChannelID, MumbleChannelID_t newChannelID) {
}

void default_onChannelExited(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t channelID) {
}

void default_onUserTalkingStateChanged(MumbleConnection_t connection, MumbleUserID_t userID, TalkingState_t talkingState) {
}

bool default_onReceiveData(MumbleConnection_t connection, MumbleUserID_t sender, const char *data, size_t dataLength, const char *dataID) {
	return false;
}

bool default_onAudioInput(short *inputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech) {
	return false;
}

bool default_onAudioSourceFetched(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID) {
	return false;
}

bool default_onAudioSourceProcessed(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID) {
	return false;
}

bool default_onAudioOutputAboutToPlay(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech) {
	return false;
}


//////////////////////////////////////////////////////////////////////////
////////////////////////ACTUAL PLUGIN IMPLEMENTATION//////////////////////
//////////////////////////////////////////////////////////////////////////


// initialize the static ID counter
uint32_t Plugin::nextID = 1;
QReadWriteLock Plugin::idLock(QReadWriteLock::Recursive);


Plugin::Plugin(QString path, QObject *p) : QObject(p), lib(path), pluginPath(path), pluginIsLoaded(false), pluginLock(QReadWriteLock::Recursive) {
	// See if the plugin is loadable in the first place
	pluginIsValid = lib.load();

	if (!pluginIsValid) {
		// throw an exception to indicate that the plugin isn't valid
		throw PluginError("Unable to load the specified library");
	}

	// aquire id-lock in order to assign an ID to this plugin
	QWriteLocker lock(&Plugin::idLock);
	pluginID = Plugin::nextID;
	Plugin::nextID++;

#ifdef MUMBLE_PLUGIN_DEBUG
	qDebug() << "Assigning ID" << pluginID << "to plugin" << pluginPath;
#endif
}

Plugin::~Plugin() {
	if (pluginIsLoaded) {
		this->unload();
	}
	if (lib.isLoaded()) {
		lib.unload();
	}
}

bool Plugin::doInitialize() {
	this->resolveFunctionPointers();

	return pluginIsValid;
}

void Plugin::resolveFunctionPointers() {
	QWriteLocker lock(&this->pluginLock);

	if (pluginIsValid) {
		// The corresponding library was loaded -> try to locate all API functions and provide defaults for
		// the missing ones
		
		// resolve the mandatory functions first
		init = reinterpret_cast<MumbleError_t (*)()>(lib.resolve("init"));
		shutdown = reinterpret_cast<void (*)()>(lib.resolve("shutdown"));
		getName = reinterpret_cast<const char* (*)()>(lib.resolve("getName"));
		getAPIVersion = reinterpret_cast<Version_t (*)()>(lib.resolve("getAPIVersion"));
		registerAPIFunctions = reinterpret_cast<void (*)(const MumbleAPI*)>(lib.resolve("registerAPIFunctions"));

		// validate that all those functions are available in the loaded lib
		pluginIsValid = init && shutdown && getName && getAPIVersion && registerAPIFunctions;

		if (!pluginIsValid) {
			// Don't bother trying to resolve any other functions
			this->setFunctionPointersToNull();

#ifdef MUMBLE_PLUGIN_DEBUG
#define CHECK_AND_LOG(name) if (!name) { qDebug("\t\"%s\" is missing the %s() function", qPrintable(pluginPath), #name); }
			CHECK_AND_LOG(init);
			CHECK_AND_LOG(shutdown);
			CHECK_AND_LOG(getName);
			CHECK_AND_LOG(getAPIVersion);
			CHECK_AND_LOG(registerAPIFunctions);
#endif

			return;
		}

		// The mandatory functions are there, now see if any optional functions are implemented as well
		setMumbleInfo = reinterpret_cast<void (*)(Version_t, Version_t, Version_t)>(lib.resolve("setMumbleInfo"));
		getVersion = reinterpret_cast<Version_t (*)()>(lib.resolve("getVersion"));
		getAuthor = reinterpret_cast<const char* (*)()>(lib.resolve("getAuthor"));
		getDescription = reinterpret_cast<const char* (*)()>(lib.resolve("getDescription"));
		registerPluginID = reinterpret_cast<void  (*)(uint32_t)>(lib.resolve("registerPluginID"));
		getPluginFeatures = reinterpret_cast<uint32_t (*)()>(lib.resolve("getPluginFeatures"));
		deactivateFeatures = reinterpret_cast<uint32_t (*)(uint32_t)>(lib.resolve("deactivateFeatures"));
		initPositionalData = reinterpret_cast<uint8_t (*)(const char**, const uint64_t **, size_t)>(lib.resolve("initPositionalData"));
		fetchPositionalData = reinterpret_cast<bool (*)(float*, float*, float*, float*, float*, float*, const char**, const char**)>(lib.resolve("initPositionalData"));
		shutDownPositionalData = reinterpret_cast<void (*)()>(lib.resolve("shutdownPositionalData"));
		onServerConnected = reinterpret_cast<void (*)(MumbleConnection_t)>(lib.resolve("onServerConnected"));
		onServerDisconnected = reinterpret_cast<void (*)(MumbleConnection_t)>(lib.resolve("onServerDisconnected"));
		onChannelEntered = reinterpret_cast<void (*)(MumbleConnection_t, MumbleUserID_t, MumbleChannelID_t, MumbleChannelID_t)>(lib.resolve("onChannelEntered"));
		onChannelExited = reinterpret_cast<void (*)(MumbleConnection_t, MumbleUserID_t, MumbleChannelID_t)>(lib.resolve("onChannelExited"));
		onUserTalkingStateChanged = reinterpret_cast<void (*)(MumbleConnection_t, MumbleUserID_t, TalkingState_t)>(lib.resolve("onUserTalkingStateChanged"));
		onAudioInput = reinterpret_cast<bool (*)(short*, uint32_t, uint16_t, bool)>(lib.resolve("onAudioInput"));
		onAudioSourceFetched = reinterpret_cast<bool (*)(float*, uint32_t, uint16_t, bool, MumbleUserID_t)>(lib.resolve("onAudioSourceFetched"));
		onAudioSourceProcessed = reinterpret_cast<bool (*)(float*, uint32_t, uint16_t, bool, MumbleUserID_t)>(lib.resolve("onAudioSourceProcessed"));
		onAudioOutputAboutToPlay = reinterpret_cast<bool (*)(float*, uint32_t, uint16_t, bool)>(lib.resolve("onAudioOutputAboutToPlay"));

		// If positional audio is to be supported, all three corresponding functions have to be implemented
		// For PA it is all or nothing
		if (!(initPositionalData && fetchPositionalData && shutDownPositionalData)
				&& (initPositionalData || fetchPositionalData || shutDownPositionalData)) {
			initPositionalData = nullptr;
			fetchPositionalData = nullptr;
			shutDownPositionalData = nullptr;
		}

		this->setDefaultImplementations();
	} else {
		// The corresponding library could not be loaded -> Set all function pointers to nullptr
		this->setFunctionPointersToNull();
	}
}

void Plugin::setFunctionPointersToNull() {
	QWriteLocker lock(&this->pluginLock);

	init = nullptr;
	shutdown = nullptr;
	getName = nullptr;
	getAPIVersion = nullptr;
	registerPluginID = nullptr;
	setMumbleInfo = nullptr;
	getVersion = nullptr;
	getAuthor = nullptr;
	getDescription = nullptr;
	registerPluginID = nullptr;
	getPluginFeatures = nullptr;
	deactivateFeatures = nullptr;
	initPositionalData = nullptr;
	fetchPositionalData = nullptr;
	shutDownPositionalData = nullptr;
	onServerConnected = nullptr;
	onServerDisconnected = nullptr;
	onChannelEntered = nullptr;
	onChannelExited = nullptr;
	onUserTalkingStateChanged = nullptr;
	onReceiveData = nullptr;
	onAudioInput = nullptr;
	onAudioSourceFetched = nullptr;
	onAudioSourceProcessed = nullptr;
	onAudioOutputAboutToPlay = nullptr;
}

/// Checks whether the given function pointer is set and if it isn't, it will set it to point to
/// its default implementation which is assumed to be named "default_funcName".
///
/// @param funcName The name of the pointer to check
#define CHECK_AND_SET_DEFAULT(funcName) if (!funcName) { funcName = &default_ ## funcName; }

void Plugin::setDefaultImplementations() {
	QWriteLocker lock(&this->pluginLock);

	CHECK_AND_SET_DEFAULT(setMumbleInfo);
	CHECK_AND_SET_DEFAULT(getVersion);
	CHECK_AND_SET_DEFAULT(getAuthor);
	CHECK_AND_SET_DEFAULT(getDescription);
	CHECK_AND_SET_DEFAULT(registerPluginID);
	CHECK_AND_SET_DEFAULT(getPluginFeatures);
	CHECK_AND_SET_DEFAULT(deactivateFeatures);
	CHECK_AND_SET_DEFAULT(initPositionalData);
	CHECK_AND_SET_DEFAULT(fetchPositionalData);
	CHECK_AND_SET_DEFAULT(shutDownPositionalData);
	CHECK_AND_SET_DEFAULT(onServerConnected);
	CHECK_AND_SET_DEFAULT(onServerDisconnected);
	CHECK_AND_SET_DEFAULT(onChannelEntered);
	CHECK_AND_SET_DEFAULT(onChannelExited);
	CHECK_AND_SET_DEFAULT(onUserTalkingStateChanged);
	CHECK_AND_SET_DEFAULT(onReceiveData);
	CHECK_AND_SET_DEFAULT(onAudioInput);
	CHECK_AND_SET_DEFAULT(onAudioSourceFetched);
	CHECK_AND_SET_DEFAULT(onAudioSourceProcessed);
	CHECK_AND_SET_DEFAULT(onAudioOutputAboutToPlay);
}

bool Plugin::isValid() {
	QReadLocker lock(&this->pluginLock);

	return pluginIsValid;
}

bool Plugin::isLoaded() {
	QReadLocker lock(&this->pluginLock);

	return pluginIsLoaded;
}

MumbleError_t Plugin::load() {
	QWriteLocker lock(&this->pluginLock);

	if (pluginIsLoaded) {
		return STATUS_OK;
	}

	pluginIsLoaded = true;

	return this->init();
}

void Plugin::unload() {
	QWriteLocker lock(&this->pluginLock);

	if (!pluginIsLoaded) {
		return;
	}

	pluginIsLoaded = false;

	// TODO: check PA and maybe shutdown as well
	this->shutdown();
}


