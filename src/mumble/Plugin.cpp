// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Plugin.h"
#include <QtCore/QWriteLocker>
#include <QtCore/QMutexLocker>


// initialize the static ID counter
uint32_t Plugin::nextID = 1;
QMutex Plugin::idLock(QMutex::Recursive);


Plugin::Plugin(QString path, QObject *p) : QObject(p), lib(path), pluginPath(path), pluginIsLoaded(false), pluginLock(QReadWriteLock::Recursive),
	apiFnc() {
	// See if the plugin is loadable in the first place
	pluginIsValid = lib.load();

	if (!pluginIsValid) {
		// throw an exception to indicate that the plugin isn't valid
		throw PluginError("Unable to load the specified library");
	}

	// aquire id-lock in order to assign an ID to this plugin
	QMutexLocker lock(&Plugin::idLock);
	pluginID = Plugin::nextID;
	Plugin::nextID++;
}

Plugin::~Plugin() {
	if (this->isLoaded()) {
		this->shutdown();
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

	if (this->isValid()) {
		// The corresponding library was loaded -> try to locate all API functions and provide defaults for
		// the missing ones
		
		// resolve the mandatory functions first
		this->apiFnc.init = reinterpret_cast<MumbleError_t (*)()>(lib.resolve("init"));
		this->apiFnc.shutdown = reinterpret_cast<void (*)()>(lib.resolve("shutdown"));
		this->apiFnc.getName = reinterpret_cast<const char* (*)()>(lib.resolve("getName"));
		this->apiFnc.getAPIVersion = reinterpret_cast<Version_t (*)()>(lib.resolve("getAPIVersion"));
		this->apiFnc.registerAPIFunctions = reinterpret_cast<void (*)(const MumbleAPI*)>(lib.resolve("registerAPIFunctions"));

		// validate that all those functions are available in the loaded lib
		pluginIsValid = this->apiFnc.init && this->apiFnc.shutdown && this->apiFnc.getName && this->apiFnc.getAPIVersion
			&& this->apiFnc.registerAPIFunctions;

		if (!pluginIsValid) {
			// Don't bother trying to resolve any other functions
#ifdef MUMBLE_PLUGIN_DEBUG
#define CHECK_AND_LOG(name) if (!this->apiFnc.name) { qDebug("\t\"%s\" is missing the %s() function", qPrintable(pluginPath), #name); }
			CHECK_AND_LOG(init);
			CHECK_AND_LOG(shutdown);
			CHECK_AND_LOG(getName);
			CHECK_AND_LOG(getAPIVersion);
			CHECK_AND_LOG(registerAPIFunctions);
#endif

			return;
		}

		// The mandatory functions are there, now see if any optional functions are implemented as well
		this->apiFnc.setMumbleInfo = reinterpret_cast<void (*)(Version_t, Version_t, Version_t)>(lib.resolve("setMumbleInfo"));
		this->apiFnc.getVersion = reinterpret_cast<Version_t (*)()>(lib.resolve("getVersion"));
		this->apiFnc.getAuthor = reinterpret_cast<const char* (*)()>(lib.resolve("getAuthor"));
		this->apiFnc.getDescription = reinterpret_cast<const char* (*)()>(lib.resolve("getDescription"));
		this->apiFnc.registerPluginID = reinterpret_cast<void  (*)(uint32_t)>(lib.resolve("registerPluginID"));
		this->apiFnc.getPluginFeatures = reinterpret_cast<uint32_t (*)()>(lib.resolve("getPluginFeatures"));
		this->apiFnc.deactivateFeatures = reinterpret_cast<uint32_t (*)(uint32_t)>(lib.resolve("deactivateFeatures"));
		this->apiFnc.initPositionalData = reinterpret_cast<uint8_t (*)(const char**, const uint64_t *, size_t)>(lib.resolve("initPositionalData"));
		this->apiFnc.fetchPositionalData = reinterpret_cast<bool (*)(float*, float*, float*, float*, float*, float*, const char**, const char**)>(lib.resolve("fetchPositionalData"));
		this->apiFnc.shutdownPositionalData = reinterpret_cast<void (*)()>(lib.resolve("shutdownPositionalData"));
		this->apiFnc.onServerConnected = reinterpret_cast<void (*)(MumbleConnection_t)>(lib.resolve("onServerConnected"));
		this->apiFnc.onServerDisconnected = reinterpret_cast<void (*)(MumbleConnection_t)>(lib.resolve("onServerDisconnected"));
		this->apiFnc.onChannelEntered = reinterpret_cast<void (*)(MumbleConnection_t, MumbleUserID_t, MumbleChannelID_t, MumbleChannelID_t)>(lib.resolve("onChannelEntered"));
		this->apiFnc.onChannelExited = reinterpret_cast<void (*)(MumbleConnection_t, MumbleUserID_t, MumbleChannelID_t)>(lib.resolve("onChannelExited"));
		this->apiFnc.onUserTalkingStateChanged = reinterpret_cast<void (*)(MumbleConnection_t, MumbleUserID_t, TalkingState_t)>(lib.resolve("onUserTalkingStateChanged"));
		this->apiFnc.onAudioInput = reinterpret_cast<bool (*)(short*, uint32_t, uint16_t, bool)>(lib.resolve("onAudioInput"));
		this->apiFnc.onAudioSourceFetched = reinterpret_cast<bool (*)(float*, uint32_t, uint16_t, bool, MumbleUserID_t)>(lib.resolve("onAudioSourceFetched"));
		this->apiFnc.onAudioSourceProcessed = reinterpret_cast<bool (*)(float*, uint32_t, uint16_t, bool, MumbleUserID_t)>(lib.resolve("onAudioSourceProcessed"));
		this->apiFnc.onAudioOutputAboutToPlay = reinterpret_cast<bool (*)(float*, uint32_t, uint16_t, bool)>(lib.resolve("onAudioOutputAboutToPlay"));

		// If positional audio is to be supported, all three corresponding functions have to be implemented
		// For PA it is all or nothing
		if (!(this->apiFnc.initPositionalData && this->apiFnc.fetchPositionalData && this->apiFnc.shutdownPositionalData)
				&& (this->apiFnc.initPositionalData || this->apiFnc.fetchPositionalData || this->apiFnc.shutdownPositionalData)) {
			this->apiFnc.initPositionalData = nullptr;
			this->apiFnc.fetchPositionalData = nullptr;
			this->apiFnc.shutdownPositionalData = nullptr;
#ifdef MUMBLE_PLUGIN_DEBUG
			qDebug("\t\"%s\" has only partially implemented positional audio functions -> deactivating all of them", qPrintable(pluginPath));
#endif
		}
	}
}

/*void Plugin::setFunctionPointersToNull() {
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
}*/

bool Plugin::isValid() const {
	PluginReadLocker lock(&this->pluginLock);

	return pluginIsValid;
}

bool Plugin::isLoaded() const {
	PluginReadLocker lock(&this->pluginLock);

	return pluginIsLoaded;
}

MumbleError_t Plugin::init() {
	QWriteLocker lock(&this->pluginLock);

	if (pluginIsLoaded) {
		return STATUS_OK;
	}

	pluginIsLoaded = true;

	if (this->apiFnc.init) {
		return this->apiFnc.init();
	} else {
		// If there's no such function nothing can go wrong because nothing was called
		return STATUS_OK;
	}
}

void Plugin::shutdown() {
	QWriteLocker lock(&this->pluginLock);

	if (!pluginIsLoaded) {
		return;
	}

	pluginIsLoaded = false;

	// TODO: check PA and maybe shutdown as well
	if (this->apiFnc.shutdown) {
		this->shutdown();
	}
}

const char* Plugin::getName() const {
	if (this->apiFnc.getName) {
		return this->apiFnc.getName();
	} else {
		return "Unknown plugin";
	}
}

Version_t Plugin::getAPIVersion() const {
	if (this->apiFnc.getAPIVersion) {
		return this->apiFnc.getAPIVersion();
	} else {
		return {-1, -1 , -1};
	}
}

void Plugin::registerAPIFunctions(const MumbleAPI *api) {
	if (this->apiFnc.registerAPIFunctions) {
		this->registerAPIFunctions(api);
	}
}


void Plugin::setMumbleInfo(Version_t mumbleVersion, Version_t mumbleAPIVersion, Version_t minimalExpectedAPIVersion) {
	if (this->apiFnc.setMumbleInfo) {
		this->apiFnc.setMumbleInfo(mumbleVersion, mumbleAPIVersion, minimalExpectedAPIVersion);
	}
}

Version_t Plugin::getVersion() const {
	if (this->apiFnc.getVersion) {
		return this->apiFnc.getVersion();
	} else {
		return {0, 0, 0};
	}
}

const char* Plugin::getAuthor() const {
	if (this->apiFnc.getAuthor) {
		return this->apiFnc.getAuthor();
	} else {
		return "Unknown";
	}
}

const char* Plugin::getDescription() const {
	if (this->apiFnc.getDescription) {
		return this->apiFnc.getDescription();
	} else {
		return "No description provided";
	}
}

void Plugin::registerPluginID(uint32_t id) {
	if (this->apiFnc.registerPluginID) {
		this->apiFnc.registerPluginID(id);
	}
}

uint32_t Plugin::getPluginFeatures() const {
	if (this->apiFnc.getPluginFeatures) {
		return this->apiFnc.getPluginFeatures();
	} else {
		return FEATURE_NONE;
	}
}

uint32_t Plugin::deactivateFeatures(uint32_t features) {
	if (this->apiFnc.deactivateFeatures) {
		return this->apiFnc.deactivateFeatures(features);
	} else {
		return features;
	}
}

uint8_t Plugin::initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount) {
	if (this->apiFnc.initPositionalData) {
		return this->apiFnc.initPositionalData(programNames, programPIDs, programCount);
	} else {
		return PDEC_ERROR_PERM;
	}
}

#define SET_TO_ZERO_3D(arr) arr[0] = 0.0f; arr[1] = 0.0f; arr[2] = 0.0f;
bool Plugin::fetchPositionalData(float *avatar_pos, float *avatar_front, float *avatar_axis, float *camera_pos, float *camera_front,
		float *camera_axis, const char **context, const char **identity) {
	if (this->apiFnc.fetchPositionalData) {
		return this->fetchPositionalData(avatar_pos, avatar_front, avatar_axis, camera_pos, camera_front, camera_axis, context, identity);
	} else {
		SET_TO_ZERO_3D(avatar_pos);
		SET_TO_ZERO_3D(avatar_front);
		SET_TO_ZERO_3D(avatar_axis);
		SET_TO_ZERO_3D(camera_pos);
		SET_TO_ZERO_3D(camera_front);
		SET_TO_ZERO_3D(camera_axis);
		context = nullptr;
		identity = nullptr;
		
		return false;
	}
}
#undef SET_TO_ZERO_3D

void Plugin::shutdownPositionalData() {
	if (this->apiFnc.shutdownPositionalData) {
		this->apiFnc.shutdownPositionalData();
	}
}

void Plugin::onServerConnected(MumbleConnection_t connection) {
	if (this->apiFnc.onServerConnected) {
		this->apiFnc.onServerConnected(connection);
	}
}

void Plugin::onServerDisconnected(MumbleConnection_t connection) {
	if (this->apiFnc.onServerDisconnected) {
		this->apiFnc.onServerDisconnected(connection);
	}
}

void Plugin::onChannelEntered(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t previousChannelID,
		MumbleChannelID_t newChannelID) {
	if (this->apiFnc.onChannelEntered) {
		this->apiFnc.onChannelEntered(connection, userID, previousChannelID, newChannelID);
	}
}

void Plugin::onChannelExited(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t channelID) {
	if (this->apiFnc.onChannelExited) {
		this->apiFnc.onChannelExited(connection, userID, channelID);
	}
}

void Plugin::onUserTalkingStateChanged(MumbleConnection_t connection, MumbleUserID_t userID, TalkingState_t talkingState) {
	if (this->apiFnc.onUserTalkingStateChanged) {
		this->apiFnc.onUserTalkingStateChanged(connection, userID, talkingState);
	}
}

bool Plugin::onReceiveData(MumbleConnection_t connection, MumbleUserID_t sender, const char *data, size_t dataLength, const char *dataID) {
	if (this->apiFnc.onReceiveData) {
		return this->apiFnc.onReceiveData(connection, sender, data, dataLength, dataID);
	} else {
		return false;
	}
}

bool Plugin::onAudioInput(short *inputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech) {
	if (this->apiFnc.onAudioInput) {
		return this->apiFnc.onAudioInput(inputPCM, sampleCount, channelCount, isSpeech);
	} else {
		return false;
	}
}

bool Plugin::onAudioSourceFetched(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID) {
	if (this->apiFnc.onAudioSourceFetched) {
		return this->apiFnc.onAudioSourceFetched(outputPCM, sampleCount, channelCount, isSpeech, userID);
	} else {
		return false;
	}
}

bool Plugin::onAudioSourceProcessed(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID) {
	if (this->apiFnc.onAudioSourceProcessed) {
		return this->apiFnc.onAudioSourceProcessed(outputPCM, sampleCount, channelCount, isSpeech, userID);
	} else {
		return false;
	}
}

bool Plugin::onAudioOutputAboutToPlay(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech) {
	if (this->apiFnc.onAudioOutputAboutToPlay) {
		return this->apiFnc.onAudioOutputAboutToPlay(outputPCM, sampleCount, channelCount, isSpeech);
	} else {
		return false;
	}
}



/////////////////// Implementation of the PluginReadLocker /////////////////////////
PluginReadLocker::PluginReadLocker(QReadWriteLock *lock) : lock(lock) {
	if (!lock) {
		// do nothing for a nullptr
		return;
	}

	// First try to lock for read-access
	if (!lock->tryLockForRead()) {
		// if that fails, we'll try to lock for write-access
		// That will only succeed in the case that the current thread holds the write-access to this lock already which caused
		// the previous attempt to lock for reading to fail (by design of the QtReadWriteLock).
		// As we are in the thread with the write-access, it means that this threads has asked for read-access on top of it which we will
		// grant (in contrast of QtReadLocker) because if you have the permission to change something you surely should have permission
		// to read it. This assumes that the thread won't try to read data it temporarily has corrupted.
		if (!lock->tryLockForWrite()) {
			// If we couldn't lock for write at this point, it means another thread has write-access granted by the lock so we'll have to wait
			// in order to gain regular read-access as would be with QtReadLocker
			lock->lockForRead();
		}
	}
}

PluginReadLocker::~PluginReadLocker() {
	if (lock) {
		// unlock the lock if it isn't nullptr
		lock->unlock();
	}
}

