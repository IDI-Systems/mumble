// Copyright 2019-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Plugin.h"
#include "Version.h"

#include <QtCore/QWriteLocker>
#include <QtCore/QMutexLocker>

#include <cstring>


// initialize the static ID counter
plugin_id_t Plugin::s_nextID = 1;
QMutex Plugin::s_idLock(QMutex::Recursive);

void assertPluginLoaded(const Plugin* plugin) {
	// don't throw and exception in release build
	if (!plugin->isLoaded()) {
#ifdef QT_DEBUG
		throw std::runtime_error("Attempting to access plugin but it is not loaded!");
#else
		qWarning("Plugin assertion failed: Assumed plugin with ID %d to be loaded but it wasn't!", plugin->getID());
#endif
	}
}

Plugin::Plugin(QString path, bool isBuiltIn, QObject *p)
	: QObject(p),
	  m_lib(path),
	  m_pluginPath(path),
	  m_pluginIsLoaded(false),
	  m_pluginLock(QReadWriteLock::Recursive),
	  m_apiFnc(),
	  m_isBuiltIn(isBuiltIn),
	  m_positionalDataIsEnabled(true),
	  m_positionalDataIsActive(false),
	  m_mayMonitorKeyboard(false) {
	// See if the plugin is loadable in the first place unless it is a built-in plugin
	m_pluginIsValid = isBuiltIn || m_lib.load();

	if (!m_pluginIsValid) {
		// throw an exception to indicate that the plugin isn't valid
		throw PluginError("Unable to load the specified library");
	}

	// aquire id-lock in order to assign an ID to this plugin
	QMutexLocker lock(&Plugin::s_idLock);
	m_pluginID = Plugin::s_nextID;
	Plugin::s_nextID++;
}

Plugin::~Plugin() {
	QWriteLocker lock(&m_pluginLock);

	if (isLoaded()) {
		shutdown();
	}
	if (m_lib.isLoaded()) {
		m_lib.unload();
	}
}

bool Plugin::doInitialize() {
	resolveFunctionPointers();
	
	return m_pluginIsValid;
}

void Plugin::resolveFunctionPointers() {
	QWriteLocker lock(&m_pluginLock);

	if (isValid()) {
		// The corresponding library was loaded -> try to locate all API functions and provide defaults for
		// the missing ones
		
		// resolve the mandatory functions first
		m_apiFnc.init = reinterpret_cast<mumble_error_t (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t)>(m_lib.resolve("mumble_init"));
		m_apiFnc.shutdown = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_shutdown"));
		m_apiFnc.getName = reinterpret_cast<const char* (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_getName"));
		m_apiFnc.getAPIVersion = reinterpret_cast<version_t (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_getAPIVersion"));
		m_apiFnc.registerAPIFunctions = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(MumbleAPI)>(m_lib.resolve("mumble_registerAPIFunctions"));

		// validate that all those functions are available in the loaded lib
		m_pluginIsValid = m_apiFnc.init && m_apiFnc.shutdown && m_apiFnc.getName && m_apiFnc.getAPIVersion
			&& m_apiFnc.registerAPIFunctions;

		if (!m_pluginIsValid) {
			// Don't bother trying to resolve any other functions
#ifdef MUMBLE_PLUGIN_DEBUG
#define CHECK_AND_LOG(name) if (!m_apiFnc.name) { qDebug("\t\"%s\" is missing the %s() function", qPrintable(m_pluginPath), #name); }
			CHECK_AND_LOG(init);
			CHECK_AND_LOG(shutdown);
			CHECK_AND_LOG(getName);
			CHECK_AND_LOG(getAPIVersion);
			CHECK_AND_LOG(registerAPIFunctions);
#undef CHECK_AND_LOG
#endif

			return;
		}

		// The mandatory functions are there, now see if any optional functions are implemented as well
		m_apiFnc.setMumbleInfo = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(version_t, version_t, version_t)>(m_lib.resolve("mumble_setMumbleInfo"));
		m_apiFnc.getVersion = reinterpret_cast<version_t (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_getVersion"));
		m_apiFnc.getAuthor = reinterpret_cast<const char* (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_getAuthor"));
		m_apiFnc.getDescription = reinterpret_cast<const char* (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_getDescription"));
		m_apiFnc.registerPluginID = reinterpret_cast<void  (PLUGIN_CALLING_CONVENTION *)(uint32_t)>(m_lib.resolve("mumble_registerPluginID"));
		m_apiFnc.getFeatures = reinterpret_cast<uint32_t (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_getFeatures"));
		m_apiFnc.deactivateFeatures = reinterpret_cast<uint32_t (PLUGIN_CALLING_CONVENTION *)(uint32_t)>(m_lib.resolve("mumble_deactivateFeatures"));
		m_apiFnc.initPositionalData = reinterpret_cast<uint8_t (PLUGIN_CALLING_CONVENTION *)(const char**, const uint64_t *, size_t)>(m_lib.resolve("mumble_initPositionalData"));
		m_apiFnc.fetchPositionalData = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(float*, float*, float*, float*, float*, float*, const char**, const char**)>(m_lib.resolve("mumble_fetchPositionalData"));
		m_apiFnc.shutdownPositionalData = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_shutdownPositionalData"));
		m_apiFnc.onServerConnected = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t)>(m_lib.resolve("mumble_onServerConnected"));
		m_apiFnc.onServerDisconnected = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t)>(m_lib.resolve("mumble_onServerDisconnected"));
		m_apiFnc.onChannelEntered = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_userid_t, mumble_channelid_t, mumble_channelid_t)>(m_lib.resolve("mumble_onChannelEntered"));
		m_apiFnc.onChannelExited = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_userid_t, mumble_channelid_t)>(m_lib.resolve("mumble_onChannelExited"));
		m_apiFnc.onUserTalkingStateChanged = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_userid_t, talking_state_t)>(m_lib.resolve("mumble_onUserTalkingStateChanged"));
		m_apiFnc.onReceiveData = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_userid_t, const char*, size_t, const char*)>(m_lib.resolve("mumble_onReceiveData"));
		m_apiFnc.onAudioInput = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(short*, uint32_t, uint16_t, bool)>(m_lib.resolve("mumble_onAudioInput"));
		m_apiFnc.onAudioSourceFetched = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(float*, uint32_t, uint16_t, bool, mumble_userid_t)>(m_lib.resolve("mumble_onAudioSourceFetched"));
		m_apiFnc.onAudioOutputAboutToPlay = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(float*, uint32_t, uint16_t)>(m_lib.resolve("mumble_onAudioOutputAboutToPlay"));
		m_apiFnc.onServerSynchronized = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t)>(m_lib.resolve("mumble_onServerSynchronized"));
		m_apiFnc.onUserAdded = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_userid_t)>(m_lib.resolve("mumble_onUserAdded"));
		m_apiFnc.onUserRemoved = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_userid_t)>(m_lib.resolve("mumble_onUserRemoved"));
		m_apiFnc.onChannelAdded = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_channelid_t)>(m_lib.resolve("mumble_onChannelAdded"));
		m_apiFnc.onChannelRemoved = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_channelid_t)>(m_lib.resolve("mumble_onChannelRemoved"));
		m_apiFnc.onChannelRenamed = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(mumble_connection_t, mumble_channelid_t)>(m_lib.resolve("mumble_onChannelRenamed"));
		m_apiFnc.onKeyEvent = reinterpret_cast<void (PLUGIN_CALLING_CONVENTION *)(uint32_t, bool)>(m_lib.resolve("mumble_onKeyEvent"));
		m_apiFnc.hasUpdate = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)()>(m_lib.resolve("mumble_hasUpdate"));
		m_apiFnc.getUpdateDownloadURL = reinterpret_cast<bool (PLUGIN_CALLING_CONVENTION *)(char*, uint16_t, uint16_t)>(m_lib.resolve("mumble_getUpdateDownloadURL"));

#ifdef MUMBLE_PLUGIN_DEBUG
#define CHECK_AND_LOG(name) qDebug("\t" #name ": %s", (m_apiFnc.name == nullptr ? "no" : "yes"))
		qDebug(">>>> Found optional functions for plugin \"%s\"", qUtf8Printable(m_pluginPath));
		CHECK_AND_LOG(setMumbleInfo);
		CHECK_AND_LOG(getVersion);
		CHECK_AND_LOG(getAuthor);
		CHECK_AND_LOG(getDescription);
		CHECK_AND_LOG(registerPluginID);
		CHECK_AND_LOG(getFeatures);
		CHECK_AND_LOG(deactivateFeatures);
		CHECK_AND_LOG(initPositionalData);
		CHECK_AND_LOG(fetchPositionalData);
		CHECK_AND_LOG(shutdownPositionalData);
		CHECK_AND_LOG(onServerConnected);
		CHECK_AND_LOG(onServerDisconnected);
		CHECK_AND_LOG(onChannelEntered);
		CHECK_AND_LOG(onChannelExited);
		CHECK_AND_LOG(onUserTalkingStateChanged);
		CHECK_AND_LOG(onReceiveData);
		CHECK_AND_LOG(onAudioInput);
		CHECK_AND_LOG(onAudioSourceFetched);
		CHECK_AND_LOG(onAudioOutputAboutToPlay);
		CHECK_AND_LOG(onServerSynchronized);
		CHECK_AND_LOG(onUserAdded);
		CHECK_AND_LOG(onUserRemoved);
		CHECK_AND_LOG(onChannelAdded);
		CHECK_AND_LOG(onChannelRemoved);
		CHECK_AND_LOG(onChannelRenamed);
		CHECK_AND_LOG(onKeyEvent);
		CHECK_AND_LOG(hasUpdate);
		CHECK_AND_LOG(getUpdateDownloadURL);
		qDebug("<<<<");
#endif

		// If positional audio is to be supported, all three corresponding functions have to be implemented
		// For PA it is all or nothing
		if (!(m_apiFnc.initPositionalData && m_apiFnc.fetchPositionalData && m_apiFnc.shutdownPositionalData)
				&& (m_apiFnc.initPositionalData || m_apiFnc.fetchPositionalData || m_apiFnc.shutdownPositionalData)) {
			m_apiFnc.initPositionalData = nullptr;
			m_apiFnc.fetchPositionalData = nullptr;
			m_apiFnc.shutdownPositionalData = nullptr;
#ifdef MUMBLE_PLUGIN_DEBUG
			qDebug("\t\"%s\" has only partially implemented positional data functions -> deactivating all of them", qPrintable(m_pluginPath));
#endif
		}
	}
}

bool Plugin::isValid() const {
	PluginReadLocker lock(&m_pluginLock);

	return m_pluginIsValid;
}

bool Plugin::isLoaded() const {
	PluginReadLocker lock(&m_pluginLock);

	return m_pluginIsLoaded;
}

plugin_id_t Plugin::getID() const {
	PluginReadLocker lock(&m_pluginLock);

	return m_pluginID;
}

bool Plugin::isBuiltInPlugin() const {
	PluginReadLocker lock(&m_pluginLock);

	return m_isBuiltIn;
}

QString Plugin::getFilePath() const {
	PluginReadLocker lock(&m_pluginLock);

	return m_pluginPath;
}

bool Plugin::isPositionalDataEnabled() const {
	PluginReadLocker lock(&m_pluginLock);

	return m_positionalDataIsEnabled;
}

void Plugin::enablePositionalData(bool enable) {
	QWriteLocker lock(&m_pluginLock);

	m_positionalDataIsEnabled = enable;
}

bool Plugin::isPositionalDataActive() const {
	PluginReadLocker lock(&m_pluginLock);

	return m_positionalDataIsActive;
}

void Plugin::allowKeyboardMonitoring(bool allow) {
	QWriteLocker lock(&m_pluginLock);

	m_mayMonitorKeyboard = allow;
}

bool Plugin::isKeyboardMonitoringAllowed() const {
	PluginReadLocker lock(&m_pluginLock);

	return m_mayMonitorKeyboard;
}

mumble_error_t Plugin::init(mumble_connection_t connection) {
	QWriteLocker lock(&m_pluginLock);

	if (m_pluginIsLoaded) {
		return STATUS_OK;
	}

	m_pluginIsLoaded = true;

	// Get Mumble version
	int mumbleMajor, mumbleMinor, mumblePatch;
	MumbleVersion::get(&mumbleMajor, &mumbleMinor, &mumblePatch);

	// Require API version 1.0.0 as the minimal supported one
	setMumbleInfo({ mumbleMajor, mumbleMinor, mumblePatch }, MUMBLE_PLUGIN_API_VERSION, { 1, 0, 0 });

	mumble_error_t retStatus;
	if (m_apiFnc.init) {
		retStatus = m_apiFnc.init(connection);
	} else {
		// If there's no such function nothing can go wrong because nothing was called
		retStatus = STATUS_OK;
	}

	if (retStatus != STATUS_OK) {
		// loading failed
		m_pluginIsLoaded = false;
		return retStatus;
	}

	registerPluginID();

	return retStatus;
}

void Plugin::shutdown() {
	QWriteLocker lock(&m_pluginLock);

	if (!m_pluginIsLoaded) {
		return;
	}

	if (m_positionalDataIsActive) {
		shutdownPositionalData();
	}

	if (m_apiFnc.shutdown) {
		m_apiFnc.shutdown();
	}

	m_pluginIsLoaded = false;
}

QString Plugin::getName() const {
	PluginReadLocker lock(&m_pluginLock);

	if (m_apiFnc.getName) {
		return QString::fromUtf8(m_apiFnc.getName());
	} else {
		return QString::fromLatin1("Unknown plugin");
	}
}

version_t Plugin::getAPIVersion() const {
	PluginReadLocker lock(&m_pluginLock);

	if (m_apiFnc.getAPIVersion) {
		return m_apiFnc.getAPIVersion();
	} else {
		return {-1, -1 , -1};
	}
}

void Plugin::registerAPIFunctions(MumbleAPI api) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.registerAPIFunctions) {
		m_apiFnc.registerAPIFunctions(api);
	}
}

void Plugin::setMumbleInfo(version_t mumbleVersion, version_t mumbleAPIVersion, version_t minimalExpectedAPIVersion) const {
	PluginReadLocker lock(&m_pluginLock);

	if (m_apiFnc.setMumbleInfo) {
		m_apiFnc.setMumbleInfo(mumbleVersion, mumbleAPIVersion, minimalExpectedAPIVersion);
	}
}

version_t Plugin::getVersion() const {
	PluginReadLocker lock(&m_pluginLock);

	if (m_apiFnc.getVersion) {
		return m_apiFnc.getVersion();
	} else {
		return VERSION_UNKNOWN;
	}
}

QString Plugin::getAuthor() const {
	PluginReadLocker lock(&m_pluginLock);

	if (m_apiFnc.getAuthor) {
		return QString::fromUtf8(m_apiFnc.getAuthor());
	} else {
		return QString::fromLatin1("Unknown");
	}
}

QString Plugin::getDescription() const {
	PluginReadLocker lock(&m_pluginLock);

	if (m_apiFnc.getDescription) {
		return QString::fromUtf8(m_apiFnc.getDescription());
	} else {
		return QString::fromLatin1("No description provided");
	}
}

void Plugin::registerPluginID() const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.registerPluginID) {
		m_apiFnc.registerPluginID(m_pluginID);
	}
}

uint32_t Plugin::getFeatures() const {
	PluginReadLocker lock(&m_pluginLock);

	if (m_apiFnc.getFeatures) {
		return m_apiFnc.getFeatures();
	} else {
		return FEATURE_NONE;
	}
}

uint32_t Plugin::deactivateFeatures(uint32_t features) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.deactivateFeatures) {
		return m_apiFnc.deactivateFeatures(features);
	} else {
		return features;
	}
}

bool Plugin::showAboutDialog(QWidget *parent) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	Q_UNUSED(parent);
	return false;
}

bool Plugin::showConfigDialog(QWidget *parent) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	Q_UNUSED(parent);
	return false;
}

uint8_t Plugin::initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount) {
	QWriteLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.initPositionalData) {
		m_positionalDataIsActive = true;

		return m_apiFnc.initPositionalData(programNames, programPIDs, programCount);
	} else {
		return PDEC_ERROR_PERM;
	}
}

bool Plugin::fetchPositionalData(Position3D& avatarPos, Vector3D& avatarDir, Vector3D& avatarAxis, Position3D& cameraPos, Vector3D& cameraDir,
		Vector3D& cameraAxis, QString& context, QString& identity) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.fetchPositionalData) {
		const char *contextPtr = "";
		const char *identityPtr = "";

		bool retStatus = m_apiFnc.fetchPositionalData(static_cast<float*>(avatarPos), static_cast<float*>(avatarDir),
				static_cast<float*>(avatarAxis), static_cast<float*>(cameraPos), static_cast<float*>(cameraDir), static_cast<float*>(cameraAxis),
					&contextPtr, &identityPtr);

		context = QString::fromUtf8(contextPtr);
		identity = QString::fromUtf8(identityPtr);

		return retStatus;
	} else {
		avatarPos.toZero();
		avatarDir.toZero();
		avatarAxis.toZero();
		cameraPos.toZero();
		cameraDir.toZero();
		cameraAxis.toZero();
		context = QString();
		identity = QString();
		
		return false;
	}
}

void Plugin::shutdownPositionalData() {
	QWriteLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.shutdownPositionalData) {
		m_positionalDataIsActive = false;

		m_apiFnc.shutdownPositionalData();
	}
}

void Plugin::onServerConnected(mumble_connection_t connection) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onServerConnected) {
		m_apiFnc.onServerConnected(connection);
	}
}

void Plugin::onServerDisconnected(mumble_connection_t connection) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onServerDisconnected) {
		m_apiFnc.onServerDisconnected(connection);
	}
}

void Plugin::onChannelEntered(mumble_connection_t connection, mumble_userid_t userID, mumble_channelid_t previousChannelID,
		mumble_channelid_t newChannelID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onChannelEntered) {
		m_apiFnc.onChannelEntered(connection, userID, previousChannelID, newChannelID);
	}
}

void Plugin::onChannelExited(mumble_connection_t connection, mumble_userid_t userID, mumble_channelid_t channelID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onChannelExited) {
		m_apiFnc.onChannelExited(connection, userID, channelID);
	}
}

void Plugin::onUserTalkingStateChanged(mumble_connection_t connection, mumble_userid_t userID, talking_state_t talkingState) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onUserTalkingStateChanged) {
		m_apiFnc.onUserTalkingStateChanged(connection, userID, talkingState);
	}
}

bool Plugin::onReceiveData(mumble_connection_t connection, mumble_userid_t sender, const char *data, size_t dataLength, const char *dataID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onReceiveData) {
		return m_apiFnc.onReceiveData(connection, sender, data, dataLength, dataID);
	} else {
		return false;
	}
}

bool Plugin::onAudioInput(short *inputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onAudioInput) {
		return m_apiFnc.onAudioInput(inputPCM, sampleCount, channelCount, isSpeech);
	} else {
		return false;
	}
}

bool Plugin::onAudioSourceFetched(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, mumble_userid_t userID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onAudioSourceFetched) {
		return m_apiFnc.onAudioSourceFetched(outputPCM, sampleCount, channelCount, isSpeech, userID);
	} else {
		return false;
	}
}

bool Plugin::onAudioOutputAboutToPlay(float *outputPCM, uint32_t sampleCount, uint16_t channelCount) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onAudioOutputAboutToPlay) {
		return m_apiFnc.onAudioOutputAboutToPlay(outputPCM, sampleCount, channelCount);
	} else {
		return false;
	}
}

void Plugin::onServerSynchronized(mumble_connection_t connection) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onServerSynchronized) {
		m_apiFnc.onServerSynchronized(connection);
	}
}

void Plugin::onUserAdded(mumble_connection_t connection, mumble_userid_t userID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onUserAdded) {
		m_apiFnc.onUserAdded(connection, userID);
	}
}

void Plugin::onUserRemoved(mumble_connection_t connection, mumble_userid_t userID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onUserRemoved) {
		m_apiFnc.onUserRemoved(connection, userID);
	}
}

void Plugin::onChannelAdded(mumble_connection_t connection, mumble_channelid_t channelID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onChannelAdded) {
		m_apiFnc.onChannelAdded(connection, channelID);
	}
}

void Plugin::onChannelRemoved(mumble_connection_t connection, mumble_channelid_t channelID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onChannelRemoved) {
		m_apiFnc.onChannelRemoved(connection, channelID);
	}
}

void Plugin::onChannelRenamed(mumble_connection_t connection, mumble_channelid_t channelID) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (m_apiFnc.onChannelRenamed) {
		m_apiFnc.onChannelRenamed(connection, channelID);
	}
}

void Plugin::onKeyEvent(keycode_t keyCode, bool wasPress) const {
	PluginReadLocker lock(&m_pluginLock);

	assertPluginLoaded(this);

	if (!m_mayMonitorKeyboard) {
		// Keyboard monitoring is forbiden for this plugin
		return;
	}

	if (m_apiFnc.onKeyEvent) {
		m_apiFnc.onKeyEvent(keyCode, wasPress);
	}
}

bool Plugin::hasUpdate() const {
	PluginReadLocker lock(&m_pluginLock);
	
	if (m_apiFnc.hasUpdate) {
		return m_apiFnc.hasUpdate();
	} else {
		// A plugin that doesn't implement this function is assumed to never know about
		// any potential updates
		return false;
	}
}

QUrl Plugin::getUpdateDownloadURL() const {
	if (!m_apiFnc.getUpdateDownloadURL) {
		// Return an empty URL as a fallback
		return QUrl();
	}

	const int bufferSize = 150;

	QString strURL;
	char buffer[bufferSize];
	unsigned int offset = 0;

	bool readCompleteURL = false;

	while(!readCompleteURL) {
		// Clear buffer
		std::memset(buffer, 0, bufferSize);

		readCompleteURL = m_apiFnc.getUpdateDownloadURL(buffer, bufferSize, offset);

		if (buffer[bufferSize - 1] == 0) {
			// The buffer is zero-terminated
			strURL += QString::fromUtf8(buffer);
		} else {
			// The buffer is not zero-terminated
			// Thus we have to specify a size to avoid reading beyond our memory
			strURL += QString::fromUtf8(buffer, bufferSize);
		}

		offset += bufferSize;
	}

	return QUrl(strURL);
}

bool Plugin::providesAboutDialog() const {
	return false;
}

bool Plugin::providesConfigDialog() const {
	return false;
}



/////////////////// Implementation of the PluginReadLocker /////////////////////////
PluginReadLocker::PluginReadLocker(QReadWriteLock *lock)
	: m_lock(lock),
	  m_unlocked(false) {
	relock();
}

void PluginReadLocker::unlock() {
	if (!m_lock) {
		// do nothgin for nullptr
		return;
	}

	m_unlocked = true;

	m_lock->unlock();
}

void PluginReadLocker::relock() {
	if (!m_lock) {
		// do nothing for a nullptr
		return;
	}

	// First try to lock for read-access
	if (!m_lock->tryLockForRead()) {
		// if that fails, we'll try to lock for write-access
		// That will only succeed in the case that the current thread holds the write-access to this lock already which caused
		// the previous attempt to lock for reading to fail (by design of the QtReadWriteLock).
		// As we are in the thread with the write-access, it means that this threads has asked for read-access on top of it which we will
		// grant (in contrast of QtReadLocker) because if you have the permission to change something you surely should have permission
		// to read it. This assumes that the thread won't try to read data it temporarily has corrupted.
		if (!m_lock->tryLockForWrite()) {
			// If we couldn't lock for write at this point, it means another thread has write-access granted by the lock so we'll have to wait
			// in order to gain regular read-access as would be with QtReadLocker
			m_lock->lockForRead();
		}
	}

	m_unlocked = false;
}

PluginReadLocker::~PluginReadLocker() {
	if (m_lock && !m_unlocked) {
		// unlock the lock if it isn't nullptr
		m_lock->unlock();
	}
}

