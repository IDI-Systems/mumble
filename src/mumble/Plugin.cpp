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


Plugin::Plugin(QString path, bool isBuiltIn, QObject *p) : QObject(p), lib(path), pluginPath(path), pluginIsLoaded(false), pluginLock(QReadWriteLock::Recursive),
	apiFnc(), isBuiltIn(isBuiltIn) {
	// See if the plugin is loadable in the first place unless it is a built-in plugin
	pluginIsValid = isBuiltIn || lib.load();

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
	QWriteLocker lock(&this->pluginLock);

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
		this->apiFnc.init = reinterpret_cast<MumbleError_t (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("init"));
		this->apiFnc.shutdown = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("shutdown"));
		this->apiFnc.getName = reinterpret_cast<const char* (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("getName"));
		this->apiFnc.getAPIVersion = reinterpret_cast<Version_t (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("getAPIVersion"));
		this->apiFnc.registerAPIFunctions = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(const MumbleAPI*)>(lib.resolve("registerAPIFunctions"));

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
		this->apiFnc.setMumbleInfo = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(Version_t, Version_t, Version_t)>(lib.resolve("setMumbleInfo"));
		this->apiFnc.getVersion = reinterpret_cast<Version_t (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("getVersion"));
		this->apiFnc.getAuthor = reinterpret_cast<const char* (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("getAuthor"));
		this->apiFnc.getDescription = reinterpret_cast<const char* (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("getDescription"));
		this->apiFnc.registerPluginID = reinterpret_cast<void  (PLUGIN_CALLING_CONVENTION *)(uint32_t)>(lib.resolve("registerPluginID"));
		this->apiFnc.getPluginFeatures = reinterpret_cast<uint32_t (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("getPluginFeatures"));
		this->apiFnc.deactivateFeatures = reinterpret_cast<uint32_t (PLUGIN_CALLING_CONVENTION *)(uint32_t)>(lib.resolve("deactivateFeatures"));
		this->apiFnc.initPositionalData = reinterpret_cast<uint8_t (PLUGIN_CALLING_CONVENTION *)(const char**, const uint64_t *, size_t)>(lib.resolve("initPositionalData"));
		this->apiFnc.fetchPositionalData = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(float*, float*, float*, float*, float*, float*, const char**, const char**)>(lib.resolve("fetchPositionalData"));
		this->apiFnc.shutdownPositionalData = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)()>(lib.resolve("shutdownPositionalData"));
		this->apiFnc.onServerConnected = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(MumbleConnection_t)>(lib.resolve("onServerConnected"));
		this->apiFnc.onServerDisconnected = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(MumbleConnection_t)>(lib.resolve("onServerDisconnected"));
		this->apiFnc.onChannelEntered = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(MumbleConnection_t, MumbleUserID_t, MumbleChannelID_t, MumbleChannelID_t)>(lib.resolve("onChannelEntered"));
		this->apiFnc.onChannelExited = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(MumbleConnection_t, MumbleUserID_t, MumbleChannelID_t)>(lib.resolve("onChannelExited"));
		this->apiFnc.onUserTalkingStateChanged = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(MumbleConnection_t, MumbleUserID_t, TalkingState_t)>(lib.resolve("onUserTalkingStateChanged"));
		this->apiFnc.onAudioInput = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(short*, uint32_t, uint16_t, bool)>(lib.resolve("onAudioInput"));
		this->apiFnc.onAudioSourceFetched = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(float*, uint32_t, uint16_t, bool, MumbleUserID_t)>(lib.resolve("onAudioSourceFetched"));
		this->apiFnc.onAudioSourceProcessed = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(float*, uint32_t, uint16_t, bool, MumbleUserID_t)>(lib.resolve("onAudioSourceProcessed"));
		this->apiFnc.onAudioOutputAboutToPlay = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(float*, uint32_t, uint16_t, bool)>(lib.resolve("onAudioOutputAboutToPlay"));

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

bool Plugin::isValid() const {
	PluginReadLocker lock(&this->pluginLock);

	return pluginIsValid;
}

bool Plugin::isLoaded() const {
	PluginReadLocker lock(&this->pluginLock);

	return pluginIsLoaded;
}

uint32_t Plugin::getID() const {
	PluginReadLocker lock(&this->pluginLock);

	return this->pluginID;
}

bool Plugin::isBuiltInPlugin() const {
	PluginReadLocker lock(&this->pluginLock);

	return this->isBuiltIn;
}

QString Plugin::getFilePath() const {
	PluginReadLocker lock(&this->pluginLock);

	return this->pluginPath;
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

QString Plugin::getName() const {
	if (this->apiFnc.getName) {
		return QString::fromUtf8(this->apiFnc.getName());
	} else {
		return QString::fromUtf8("Unknown plugin");
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

QString Plugin::getAuthor() const {
	if (this->apiFnc.getAuthor) {
		return QString::fromUtf8(this->apiFnc.getAuthor());
	} else {
		return QString::fromUtf8("Unknown");
	}
}

QString Plugin::getDescription() const {
	if (this->apiFnc.getDescription) {
		return QString::fromUtf8(this->apiFnc.getDescription());
	} else {
		return QString::fromUtf8("No description provided");
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

bool Plugin::showAboutDialog(QWidget *parent) const {
	return false;
}

bool Plugin::showConfigDialog(QWidget *parent) const {
	return false;
}

uint8_t Plugin::initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount) {
	if (this->apiFnc.initPositionalData) {
		return this->apiFnc.initPositionalData(programNames, programPIDs, programCount);
	} else {
		return PDEC_ERROR_PERM;
	}
}

#define SET_TO_ZERO_3D(arr) arr[0] = 0.0f; arr[1] = 0.0f; arr[2] = 0.0f;
bool Plugin::fetchPositionalData(float *avatarPos, float *avatarDir, float *avatarAxis, float *cameraPos, float *cameraDir,
		float *cameraAxis, const char **context, const char **identity) {
	if (this->apiFnc.fetchPositionalData) {
		return this->fetchPositionalData(avatarPos, avatarDir, avatarAxis, cameraPos, cameraDir, cameraAxis, context, identity);
	} else {
		SET_TO_ZERO_3D(avatarPos);
		SET_TO_ZERO_3D(avatarDir);
		SET_TO_ZERO_3D(avatarAxis);
		SET_TO_ZERO_3D(cameraPos);
		SET_TO_ZERO_3D(cameraDir);
		SET_TO_ZERO_3D(cameraAxis);
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

bool Plugin::providesAboutDialog() const {
	return false;
}

bool Plugin::providesConfigDialog() const {
	return false;
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

