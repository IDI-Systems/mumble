/**
 * This header file contains definitions of types and other components used in Mumble's plugin system
 */

#ifndef MUMBLE_PLUGINCOMPONENT_H_
#define MUMBLE_PLUGINCOMPONENT_H_

#include <stdint.h>
#include <stddef.h>

#define STATUS_OK EC_OK

enum PluginFeature {
	/// None of the below
	FEATURE_NONE = 0,
	/// The plugin provides positional data from a game
	FEATURE_POSITIONAL = 1 << 0,
	/// The plugin modifies the input/output audio itself
	FEATURE_AUDIO = 1 << 1
};

enum TalkingState {
	INVALID=-1,
	PASSIVE=0,
	TALKING,
	WHISPERING,
	SHOUTING
};

enum TransmissionMode {
	CONTINOUS,
	VOICE_ACTIVATION,
	PUSH_TO_TALK
};

enum ErrorCode {
	EC_OK = 0,
	EC_GENERIC_ERROR
};

enum PositionalDataErrorCode {
	/// Positional data has been initialized properly
	PDEC_OK = 0,
	/// Positional data is temporarily unavailable (e.g. because the corresponding process isn't running) but might be
	/// at another point in time.
	PDEC_ERROR_TEMP,
	/// Positional data is permanently unavailable (e.g. because the respective memory offsets are outdated).
	PDEC_ERROR_PERM
};

struct Version {
	int32_t major;
	int32_t minor;
	int32_t patch;
};


typedef enum TalkingState TalkingState_t;
typedef enum TransmissionMode TransmissionMode_t;
typedef struct Version Version_t;
typedef int32_t MumbleConnection_t;
typedef uint32_t MumbleUserID_t;
typedef int32_t MumbleChannelID_t;
typedef enum ErrorCode MumbleError_t;


struct MumbleAPI {
	// -------- Memory management --------
	
	/// Frees the given pointer.
	///
	/// @param pointer The pointer to free
	/// @returns The error code. If everything went well, STATUS_OK will be returned.
	MumbleError_t (*freeMemory)(void *pointer);


	
	// -------- Getter functions --------

	/// Gets the connection ID of the server the user is currently active on (the user's audio output is directed at).
	///
	/// @param[out] connection A pointer to the memory location the ID should be written to
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then it is valid to access the
	/// 	value of the provided pointer
	MumbleError_t (*getActiveServerConnection)(MumbleConnection_t *connection);

	/// Fills in the information about the local user.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param[out] userID A pointer to the memory the user's ID shall be written to
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer
	/// 	may be accessed
	MumbleError_t (*getLocalUserID)(MumbleConnection_t connection, MumbleUserID_t *userID);

	/// Fills in the information about the given user's name.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param userID The user's ID whose name should be obtained
	/// @param[out] userName A pointer to where the pointer to the allocated string (C-encoded) should be written to. The
	/// 	allocated memory has to be freed by a call to freeMemory by the plugin eventually. The memory will only be
	/// 	allocated if this function returns STATUS_OK.
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer
	/// 	may be accessed
	MumbleError_t (*getUserName)(MumbleConnection_t connection, MumbleUserID_t userID, const char **userName);

	/// Fills in the information about the given channel's name.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param channelID The channel's ID whose name should be obtained
	/// @param[out] channelName A pointer to where the pointer to the allocated string (C-ecoded) should be written to. The
	/// 	allocated memory has to be freed by a call to freeMemory by the plugin eventually. The memory will only be
	/// 	allocated if this function returns STATUS_OK.
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer
	/// 	may be accessed
	MumbleError_t (*getChannelName)(MumbleConnection_t connection, MumbleChannelID_t channelID, const char **channelName);

	/// Gets an array of all users that are currently connected to the provided server. Passing a nullptr as any of the out-parameter
	/// will prevent that property to be set/allocated. If you are only interested in the user count you can thus pass nullptr as the
	/// users parameter and save time on allocating + freeing the channels-array while still getting the size out.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param[out] users A pointer to where the pointer of the allocated array shall be written. The
	/// 	allocated memory has to be freed by a call to freeMemory by the plugin eventually. The memory will only be
	/// 	allocated if this function returns STATUS_OK.
	/// @param[out] userCount A pointer to where the size of the allocated user-array shall be written to
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer
	/// 	may be accessed
	MumbleError_t (*getAllUsers)(MumbleConnection_t connection, MumbleUserID_t **users, size_t *userCount);

	/// Gets an array of all channels on the provided server. Passing a nullptr as any of the out-parameter will prevent
	/// that property to be set/allocated. If you are only interested in the channel count you can thus pass nullptr as the
	/// channels parameter and save time on allocating + freeing the channels-array while still getting the size out.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param[out] channels A pointer to where the pointer of the allocated array shall be written. The
	/// 	allocated memory has to be freed by a call to freeMemory by the plugin eventually. The memory will only be
	/// 	allocated if this function returns STATUS_OK.
	/// @param[out] channelCount A pointer to where the size of the allocated channel-array shall be written to
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer
	/// 	may be accessed
	MumbleError_t (*getAllChannels)(MumbleConnection_t connection, MumbleChannelID_t **channels, size_t *channelCount);

	/// Gets the ID of the channel the given user is currently connected to.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param userID The ID of the user to search for
	/// @param[out] A pointer to where the ID of the channel shall be written
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer
	/// 	may be accessed
	MumbleError_t (*getChannelOfUser)(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t *channel);

	/// Gets an array of all users in the specified channel.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param channelID The ID of the channel whose users shall be retrieved
	/// @param[out] userList A pointer to where the pointer of the allocated array shall be written. The allocated memory has
	/// 	to be freed by a call to freeMemory by the plugin eventually. The memory will only be allocated if this function
	/// 	returns STATUS_OK.
	/// @param[out] userCount A pointer to where the size of the allocated user-array shall be written to
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer
	/// 	may be accessed
	MumbleError_t (*getUsersInChannel)(MumbleConnection_t connection, MumbleChannelID_t channelID, MumbleUserID_t **userList, size_t *userCount);

	/// Gets the current transmission mode of the local user.
	///
	/// @param[out] transmissionMode A pointer to where the transmission mode shall be written.
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer
	/// 	may be accessed
	MumbleError_t (*getLocalUserTransmissionMode)(TransmissionMode_t *transmissionMode);



	// -------- Request functions --------
	
	/// Requests Mumble to set the local user's transmission mode to the specified one.
	///
	/// @param transmissionMode The requested transmission mode
	/// @returns The error code. If everything went well, STATUS_OK will be returned.
	MumbleError_t (*requestLocalUserTransmissionMode)(TransmissionMode_t transmissionMode);

	/// Requests Mumble to move the given user into the given channel
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param userID The ID of the user that shall be moved
	/// @param channelID The ID of the channel to move the user to
	/// @param password The password of the target channel (encoded as a C-string). Pass NULL if the target channel does not require a
	/// 	password for entering
	/// @returns The error code. If everything went well, STATUS_OK will be returned.
	MumbleError_t (*requestUserMove)(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t channelID, const char *password);



	// -------- Find functions --------
	
	/// Fills in the information about a user with the specified name, if such a user exists. The search is case-sensitive.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param userName The respective user's name
	/// @param[out] userID A pointer to the memory the user's ID shall be written to
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer may
	/// 	be accessed.
	MumbleError_t (*findUserByName)(MumbleConnection_t connection, const char *userName, MumbleUserID_t *userID);

	/// Fills in the information about a channel with the specified name, if such a channel exists. The search is case-sensitive.
	///
	/// @param connection The ID of the server-connection to use as a context
	/// @param channelName The respective channel's name
	/// @param[out] channelID A pointer to the memory the channel's ID shall be written to
	/// @returns The error code. If everything went well, STATUS_OK will be returned. Only then the passed pointer may
	/// 	be accessed.
	MumbleError_t (*findChannelByName)(MumbleConnection_t connection, const char *channelName, MumbleChannelID_t *channelID);



	// -------- Miscellaneous --------
	
	/// Sends the provided data to the provided client(s). This kind of data can only be received by another plugin active
	/// on that client. The sent data can be seen by any active plugin on the receiving client. Therefore the sent data
	/// must not contain sensitive information or anything else that shouldn't be known by others.
	///
	/// @param connection The ID of the server-connection to send the data through (the server the given users are on)
	/// @param users An array of user IDs to send the data to
	/// @param userCount The size of the provided user-array
	/// @param data The data that shall be sent as a String
	/// @param dataLength The length of the data-string
	/// @param dataID The ID of the sent data. This has to be used by the receiving plugin(s) to figure out what to do with
	/// 	the data
	/// @returns The error code. If everything went well, STATUS_OK will be returned.
	MumbleError_t sendData(MumbleConnection_t connection, MumbleUserID_t *users, size_t userCount, const char *data, size_t dataLength,
			const char *dataID);
};


#endif
