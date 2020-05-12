// Copyright 2019-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "API.h"
#include "PluginComponents.h"
#include "ClientUser.h"
#include "Channel.h"
#include "ServerHandler.h"
#include "Settings.h"
#include "Log.h"
#include "AudioOutput.h"
#include "PluginManager.h"

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QHash>
#include <QtCore/QReadLocker>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <cstring>
#include <functional>
#include <string>

// We define a global macro called 'g'. This can lead to issues when included code uses 'g' as a type or parameter name (like protobuf 3.7 does). As such, for now, we have to make this our last include.
#include "Global.h"

#define VERIFY_PLUGIN_ID(id) if (!g.pluginManager->pluginExists(id)) { return EC_INVALID_PLUGIN_ID; }

namespace API {
	/// A "curator" that will keep track of allocated resources and how to delete them
	struct MumbleAPICurator {
		QMutex deleterMutex;
		QHash<void*, std::function<void(void*)>> deleteFunctions;

		~MumbleAPICurator() {
			// free all allocated functions
			QHash<void*, std::function<void(void*)>>::iterator it = deleteFunctions.begin();

			while (it != deleteFunctions.end()) {
				std::function<void(void*)> deleter = it.value();

				deleter(it.key());

				it++;
			}
		}
	};

	static MumbleAPICurator curator;

	// Some common delete-functions
	void defaultDeleter(void *ptr) {
		free(ptr);
	}


	//////////////////////////////////////////////
	/////////// API IMPLEMENTATION ///////////////
	//////////////////////////////////////////////

	// The description of the functions is provided in PluginComponents.h

	mumble_error_t PLUGIN_CALLING_CONVENTION freeMemory_v_1_0_x(plugin_id_t callerID, void *ptr) {
		// Don't verify plugin ID here to avoid memory leaks
		Q_UNUSED(callerID);

		QMutexLocker lock(&curator.deleterMutex);

		if (curator.deleteFunctions.contains(ptr)) {
			// Using take() instead of value() in order to remove the item from the map
			std::function<void(void*)> deleter = curator.deleteFunctions.take(ptr);

			// call the deleter to delete the pointer
			deleter(ptr);

			return STATUS_OK;
		} else {
			return EC_POINTER_NOT_FOUND;
		}
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION getActiveServerConnection_v_1_0_x(plugin_id_t callerID, mumble_connection_t *connection) {
		VERIFY_PLUGIN_ID(callerID);

		if (g.sh) {
			*connection = g.sh->getConnectionID();

			return STATUS_OK;
		} else {
			return EC_NO_ACTIVE_CONNECTION;
		}
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION getLocalUserID_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_userid_t *userID) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		*userID = g.uiSession;

		return STATUS_OK;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION getUserName_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_userid_t userID, char **name) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		ClientUser *user = ClientUser::get(userID);

		if (user) {
			size_t size = user->qsName.toUtf8().size() + 1;

			char *nameArray = reinterpret_cast<char*>(malloc(size * sizeof(char)));

			std::strcpy(nameArray, user->qsName.toUtf8().data());

			// save the allocated pointer and how to delete it
			{
				QMutexLocker lock(&curator.deleterMutex);

				curator.deleteFunctions.insert(nameArray, defaultDeleter);
			}

			*name = nameArray;

			return STATUS_OK;
		} else {
			return EC_USER_NOT_FOUND;
		}
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION getChannelName_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_channelid_t channelID, char **name) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		Channel *channel = Channel::get(channelID);

		if (channel) {
			size_t size = channel->qsName.toUtf8().size() + 1;

			char *nameArray = reinterpret_cast<char*>(malloc(size * sizeof(char)));

			std::strcpy(nameArray, channel->qsName.toUtf8().data());

			// save the allocated pointer and how to delete it
			{
				QMutexLocker lock(&curator.deleterMutex);

				curator.deleteFunctions.insert(nameArray, defaultDeleter);
			}

			*name = nameArray;

			return STATUS_OK;
		} else {
			return EC_CHANNEL_NOT_FOUND;
		}
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION getAllUsers_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_userid_t **users, size_t *userCount) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		QReadLocker userLock(&ClientUser::c_qrwlUsers);

		size_t amount = ClientUser::c_qmUsers.size();

		QHash<unsigned int, ClientUser*>::const_iterator it = ClientUser::c_qmUsers.constBegin();

		mumble_userid_t *userIDs = reinterpret_cast<mumble_userid_t*>(malloc(sizeof(mumble_userid_t) * amount));

		unsigned int index = 0;
		while (it != ClientUser::c_qmUsers.constEnd()) {
			userIDs[index] = it.key();

			it++;
			index++;
		}

		{
			QMutexLocker lock(&curator.deleterMutex);

			curator.deleteFunctions.insert(userIDs, defaultDeleter);
		}

		*users = userIDs;
		*userCount = amount;

		return STATUS_OK;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION getAllChannels_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_channelid_t **channels, size_t *channelCount) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		QReadLocker channelLock(&Channel::c_qrwlChannels);

		size_t amount = Channel::c_qhChannels.size();

		QHash<int, Channel*>::const_iterator it = Channel::c_qhChannels.constBegin();

		mumble_channelid_t *channelIDs = reinterpret_cast<mumble_channelid_t*>(malloc(sizeof(mumble_channelid_t) * amount));

		unsigned int index = 0;
		while (it != Channel::c_qhChannels.constEnd()) {
			channelIDs[index] = it.key();

			it++;
			index++;
		}

		{
			QMutexLocker lock(&curator.deleterMutex);

			curator.deleteFunctions.insert(channelIDs, defaultDeleter);
		}

		*channels = channelIDs;
		*channelCount = amount;

		return STATUS_OK;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION getChannelOfUser_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_userid_t userID, mumble_channelid_t *channel) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		ClientUser *user = ClientUser::get(userID);

		if (!user) {
			return EC_USER_NOT_FOUND;
		}

		if (user->cChannel) {
			*channel = user->cChannel->iId;

			return STATUS_OK;
		} else {
			return EC_GENERIC_ERROR;
		}
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION getUsersInChannel_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_channelid_t channelID, mumble_userid_t **userList,
			size_t *userCount) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		Channel *channel = Channel::get(channelID);

		if (!channel) {
			return EC_CHANNEL_NOT_FOUND;
		}

		size_t amount = channel->qlUsers.size();

		mumble_userid_t *userIDs = reinterpret_cast<mumble_userid_t*>(malloc(sizeof(mumble_userid_t) * amount));

		int index = 0;
		foreach(const User *currentUser, channel->qlUsers) {
			userIDs[index] = currentUser->uiSession;

			index++;
		}

		{
			QMutexLocker lock(&curator.deleterMutex);

			curator.deleteFunctions.insert(userIDs, defaultDeleter);
		}

		*userList = userIDs;
		*userCount = amount;

		return STATUS_OK;
	}


	mumble_error_t PLUGIN_CALLING_CONVENTION getLocalUserTransmissionMode_v_1_0_x(plugin_id_t callerID, transmission_mode_t *transmissionMode) {
		VERIFY_PLUGIN_ID(callerID);

		switch(g.s.atTransmit) {
			case Settings::AudioTransmit::Continuous:
				*transmissionMode = TM_CONTINOUS;
				break;
			case Settings::AudioTransmit::VAD:
				*transmissionMode = TM_VOICE_ACTIVATION;
				break;
			case Settings::AudioTransmit::PushToTalk:
				*transmissionMode = TM_PUSH_TO_TALK;
				break;
			default:
				return EC_GENERIC_ERROR;
		}

		return STATUS_OK;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION requestLocalUserTransmissionMode_v_1_0_x(plugin_id_t callerID, transmission_mode_t transmissionMode) {
		VERIFY_PLUGIN_ID(callerID);

		switch(transmissionMode) {
			case TM_CONTINOUS:
				g.s.atTransmit = Settings::AudioTransmit::Continuous;
				break;
			case TM_VOICE_ACTIVATION:
				g.s.atTransmit = Settings::AudioTransmit::VAD;
				break;
			case TM_PUSH_TO_TALK:
				g.s.atTransmit = Settings::AudioTransmit::PushToTalk;
				break;
			default:
				return EC_UNKNOWN_TRANSMISSION_MODE;
		}

		return STATUS_OK;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION requestUserMove_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_userid_t userID, mumble_channelid_t channelID,
			const char *password) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		ClientUser *user = ClientUser::get(userID);

		if (!user) {
			return EC_USER_NOT_FOUND;
		}

		Channel *channel = Channel::get(channelID);

		if (!channel) {
			return EC_CHANNEL_NOT_FOUND;
		}

		if (channel != user->cChannel) {
			// send move-request to the server only if the user is not in the channel already
			QStringList passwordList;
			if (password) {
				passwordList << QString::fromUtf8(password);
			}

			g.sh->joinChannel(user->uiSession, channel->iId, passwordList);
		}

		return STATUS_OK;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION requestMicrophoneActivationOverwrite_v_1_0_x(plugin_id_t callerID, bool activate) {
		VERIFY_PLUGIN_ID(callerID);

		PluginData::get().overwriteMicrophoneActivation.store(activate);

		return STATUS_OK;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION findUserByName_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, const char *userName, mumble_userid_t *userID) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		const QString qsUserName = QString::fromUtf8(userName);
		
		QReadLocker userLock(&ClientUser::c_qrwlUsers);

		QHash<unsigned int, ClientUser*>::const_iterator it = ClientUser::c_qmUsers.constBegin();
		while (it != ClientUser::c_qmUsers.constEnd()) {
			if (it.value()->qsName == qsUserName) {
				*userID = it.key();

				return STATUS_OK;
			}

			it++;
		}

		return EC_USER_NOT_FOUND;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION findChannelByName_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, const char *channelName, mumble_channelid_t *channelID) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		const QString qsChannelName = QString::fromUtf8(channelName);
		
		QReadLocker channelLock(&Channel::c_qrwlChannels);

		QHash<int, Channel*>::const_iterator it = Channel::c_qhChannels.constBegin();
		while (it != Channel::c_qhChannels.constEnd()) {
			if (it.value()->qsName == qsChannelName) {
				*channelID = it.key();

				return STATUS_OK;
			}

			it++;
		}

		return EC_CHANNEL_NOT_FOUND;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION sendData_v_1_0_x(plugin_id_t callerID, mumble_connection_t connection, mumble_userid_t *users, size_t userCount, const char *data,
		size_t dataLength, const char *dataID) {
		VERIFY_PLUGIN_ID(callerID);

		// Right now there can only be one connection managed by the current ServerHandler
		if (!g.sh || g.sh->getConnectionID() != connection) {
			return EC_CONNECTION_NOT_FOUND;
		}

		MumbleProto::PluginDataTransmission mpdt;
		mpdt.set_sendersession(g.uiSession);

		for (size_t i = 0; i < userCount; i++) {
			mpdt.add_receiversessions(users[i]);
		}

		mpdt.set_data(data, dataLength);
		mpdt.set_dataid(dataID);
		
		if (g.sh) {
			g.sh->sendMessage(mpdt);

			return STATUS_OK;
		} else {
			return EC_CONNECTION_NOT_FOUND;
		}
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION log_v_1_0_x(plugin_id_t callerID, const char *message) {
		// We verify the plugin manually
		const_plugin_ptr_t plugin = g.pluginManager->getPlugin(callerID);
		if (!plugin) {
			return EC_INVALID_PLUGIN_ID;
		}

		Log::logOrDefer(Log::PluginMessage,
			QString::fromLatin1("<b>%1:</b> %2").arg(plugin->getName().toHtmlEscaped()).arg(QString::fromUtf8(message).toHtmlEscaped())
		);

		return STATUS_OK;
	}

	mumble_error_t PLUGIN_CALLING_CONVENTION playSample_v_1_0_x(plugin_id_t callerID, const char *samplePath) {
		VERIFY_PLUGIN_ID(callerID);

		if (!g.ao) {
			return EC_AUDIO_NOT_AVAILABLE;
		}

		if (g.ao->playSample(QString::fromUtf8(samplePath), false)) {
			return STATUS_OK;
		} else {
			return EC_INVALID_SAMPLE;
		}
	}

	MumbleAPI getMumbleAPI_v_1_0_x() {
		return { freeMemory_v_1_0_x,
			getActiveServerConnection_v_1_0_x,
			getLocalUserID_v_1_0_x,
			getUserName_v_1_0_x,
			getChannelName_v_1_0_x,
			getAllUsers_v_1_0_x,
			getAllChannels_v_1_0_x,
			getChannelOfUser_v_1_0_x,
			getUsersInChannel_v_1_0_x,
			getLocalUserTransmissionMode_v_1_0_x,
			requestLocalUserTransmissionMode_v_1_0_x,
			requestUserMove_v_1_0_x,
			requestMicrophoneActivationOverwrite_v_1_0_x,
			findUserByName_v_1_0_x,
			findChannelByName_v_1_0_x,
			sendData_v_1_0_x,
			log_v_1_0_x,
			playSample_v_1_0_x
		};
	}

	MumbleAPI getMumbleAPI(const version_t& apiVersion) {
		// Select the set of API functions for the requested API version
		// as the patch-version must not involve any API changes, it doesn't hve to be considered here
		switch (apiVersion.major) {
			case 1:
				switch (apiVersion.minor) {
					case 0:
						// v1.0.x
						return getMumbleAPI_v_1_0_x();
				}
		}

		// There appears to be no API for the provided version
		throw std::invalid_argument(std::string("No API functions for API version v") + std::to_string(apiVersion.major) + "."
				+ std::to_string(apiVersion.minor) + ".x");
	}

#define MAP(qtName, apiName) case Qt::Key_##qtName: return KC_##apiName

	keycode_t qtKeyCodeToAPIKeyCode(unsigned int keyCode) {
		switch (keyCode) {
			MAP(Escape, ESCAPE);
			MAP(Tab, TAB);
			MAP(Backspace, BACKSPACE);
			case Qt::Key_Return:
				// Fallthrough
			case Qt::Key_Enter:
				return KC_ENTER;
			MAP(Delete, DELETE);
			MAP(Print, PRINT);
			MAP(Home, HOME);
			MAP(End, END);
			MAP(Up, UP);
			MAP(Down, DOWN);
			MAP(Left, LEFT);
			MAP(Right, RIGHT);
			MAP(PageUp, PAGE_UP);
			MAP(PageDown, PAGE_DOWN);
			MAP(Shift, SHIFT);
			MAP(Control, CONTROL);
			MAP(Meta, META);
			MAP(Alt, ALT);
			MAP(AltGr, ALT_GR);
			MAP(CapsLock, CAPSLOCK);
			MAP(NumLock, NUMLOCK);
			MAP(ScrollLock, SCROLLLOCK);
			MAP(F1, F1);
			MAP(F2, F2);
			MAP(F3, F3);
			MAP(F4, F4);
			MAP(F5, F5);
			MAP(F6, F6);
			MAP(F7, F7);
			MAP(F8, F8);
			MAP(F9, F9);
			MAP(F10, F10);
			MAP(F11, F11);
			MAP(F12, F12);
			MAP(F13, F13);
			MAP(F14, F14);
			MAP(F15, F15);
			MAP(F16, F16);
			MAP(F17, F17);
			MAP(F18, F18);
			MAP(F19, F19);
			case Qt::Key_Super_L:
				// Fallthrough
			case Qt::Key_Super_R:
				return KC_SUPER;
			MAP(Space, SPACE);
			MAP(Exclam, EXCLAMATION_MARK);
			MAP(QuoteDbl, DOUBLE_QUOTE);
			MAP(NumberSign, HASHTAG);
			MAP(Dollar, DOLLAR);
			MAP(Percent, PERCENT);
			MAP(Ampersand, AMPERSAND);
			MAP(Apostrophe, SINGLE_QUOTE);
			MAP(ParenLeft, OPEN_PARENTHESIS);
			MAP(ParenRight, CLOSE_PARENTHESIS);
			MAP(Asterisk, ASTERISK);
			MAP(Plus, PLUS);
			MAP(Comma, COMMA);
			MAP(Minus, MINUS);
			MAP(Period, PERIOD);
			MAP(Slash, SLASH);
			MAP(0, 0);
			MAP(1, 1);
			MAP(2, 2);
			MAP(3, 3);
			MAP(4, 4);
			MAP(5, 5);
			MAP(6, 6);
			MAP(7, 7);
			MAP(8, 8);
			MAP(9, 9);
			MAP(Colon, COLON);
			MAP(Semicolon, SEMICOLON);
			MAP(Less, LESS_THAN);
			MAP(Equal, EQUALS);
			MAP(Greater, GREATER_THAN);
			MAP(Question, QUESTION_MARK);
			MAP(At, AT_SYMBOL);
			MAP(A, A);
			MAP(B, B);
			MAP(C, C);
			MAP(D, D);
			MAP(E, E);
			MAP(F, F);
			MAP(G, G);
			MAP(H, H);
			MAP(I, I);
			MAP(J, J);
			MAP(K, K);
			MAP(L, L);
			MAP(M, M);
			MAP(N, N);
			MAP(O, O);
			MAP(P, P);
			MAP(Q, Q);
			MAP(R, R);
			MAP(S, S);
			MAP(T, T);
			MAP(U, U);
			MAP(V, V);
			MAP(W, W);
			MAP(X, X);
			MAP(Y, Y);
			MAP(Z, Z);
			MAP(BracketLeft, OPEN_BRACKET);
			MAP(BracketRight, CLOSE_BRACKET);
			MAP(Backslash, BACKSLASH);
			MAP(AsciiCircum, CIRCUMFLEX);
			MAP(Underscore, UNDERSCORE);
			MAP(BraceLeft, OPEN_BRACE);
			MAP(BraceRight, CLOSE_BRACE);
			MAP(Bar, VERTICAL_BAR);
			MAP(AsciiTilde, TILDE);
			MAP(degree, DEGREE_SIGN);

			default:
				return KC_INVALID;
		}
	}

#undef MAP


	// Implementation of PluginData
	PluginData::PluginData() : overwriteMicrophoneActivation(false) {
	}
	
	PluginData::~PluginData() {
	}

	PluginData& PluginData::get() {
		static PluginData *instance = new PluginData();

		return *instance;
	}
}; // namespace API
