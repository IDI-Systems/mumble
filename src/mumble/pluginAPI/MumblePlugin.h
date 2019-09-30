/// @file pluginAPI/Plugin.h


#ifndef EXTERNAL_MUMBLE_PLUGIN_H_
#define EXTERNAL_MUMBLE_PLUGIN_H_

#include "PluginComponents.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(__GNUC__) && !defined(__MINGW32__) // GCC on Unix-like systems
	#define PLUGIN_EXPORT __attribute__((visibility("default")))
#elif defined(_MSC_VER)
	#define PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__MINGW32__)
	#define PLUGIN_EXPORT __attribute__((dllexport))
#else
	#error No PLUGIN_EXPORT definition available
#endif

#ifdef __cplusplus
extern "C" {
#endif

	// functions for init and de-init
	
	/// Gets called right after loading the plugin in order to let the plugin initialize.
	///
	/// @returns The status of the initialization. If everything went fine, return STATUS_OK
	PLUGIN_EXPORT MumbleError_t PLUGIN_CALLING_CONVENTION init();
	
	/// Gets called when unloading the plugin in order to allow it to clean up after itself.
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION shutdown();

	/// Tells the plugin some basic information about the Mumble client loading it.
	/// This function will be the first one that is being called on this plugin - even before it is decided whether to load
	/// the plugin at all.
	///
	/// @param mumbleVersion The Version of the Mumble client
	/// @param mumbleAPIVersion The Version of the plugin-API the Mumble client runs with
	/// @param minimalExpectedAPIVersion The minimal Version the Mumble clients expects this plugin to meet in order to load it
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION setMumbleInfo(Version_t mumbleVersion, Version_t mumbleAPIVersion, Version_t minimalExpectedAPIVersion);

	// functions for general plugin info
	
	/// Gets the name of the plugin. The plugin has to guarantee that the returned pointer will still be valid. The string will be copied
	/// for further usage though.
	///
	/// @returns A pointer to the plugin name (encoded as a C-String)
	PLUGIN_EXPORT const char* PLUGIN_CALLING_CONVENTION getName();

	/// Gets the Version of this plugin
	///
	/// @returns The plugin's version
	PLUGIN_EXPORT Version_t PLUGIN_CALLING_CONVENTION getVersion();

	/// Gets the Version of the plugin-API this plugin intends to use.
	/// Mumble will decide whether this plugin is loadable or not based on the return value of this function.
	///
	/// @return The respective API Version
	PLUGIN_EXPORT Version_t PLUGIN_CALLING_CONVENTION getAPIVersion();

	/// Gets the name of the plugin author(s). The plugin has to guarantee that the returned pointer will still be valid. The string will
	/// be copied for further usage though.
	///
	/// @returns A pointer to the author(s) name(s) (encoded as a C-String)
	PLUGIN_EXPORT const char* PLUGIN_CALLING_CONVENTION getAuthor();

	/// Gets the description of the plugin. The plugin has to guarantee that the returned pointer will still be valid. The string will
	/// be copied for further usage though.
	///
	/// @returns A pointer to the description (encoded as a C-String)
	PLUGIN_EXPORT const char* PLUGIN_CALLING_CONVENTION getDescription();

	/// Provides the MumbleAPI struct to the plugin. This struct contains function pointers that can be used
	/// to interact with the Mumble client. It is up to the plugin to store this struct somewhere if it wants to make use
	/// of it at some point.
	///
	/// @param api The MumbleAPI struct
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION registerAPIFunctions(const struct MumbleAPI *api);

	/// Registers the ID of this plugin. This is the ID Mumble will reference this plugin with and by which this plugin
	/// can identify itself when communicating with Mumble.
	///
	/// @param id The ID for this plugin
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION registerPluginID(uint32_t id);
	
	/// Gets the feature set of this plugin. The feature set is described by bitwise or'ing the elements of the PluginFeature enum
	/// together.
	///
	/// @returns The feature set of this plugin
	PLUGIN_EXPORT uint32_t PLUGIN_CALLING_CONVENTION getPluginFeatures();

	/// Requests this plugin to deactivate the given (sub)set of provided features.
	/// If this is not possible, the features that can't be deactivated shall be returned by this function.
	/// 
	/// Example (check if FEATURE_POSITIONAL shall be deactivated):
	/// @code
	/// if (features & FEATURE_POSITIONAL) {
	/// 	// positional shall be deactivated
	/// };
	/// @endcode
	///
	/// @param features The feature set that shall be deactivated
	/// @returns The feature set that can't be disabled (bitwise or'ed). If all requested features can be disabled, return
	/// 	FEATURE_NONE. If none of the requested features can be disabled return the unmodified features parameter.
	PLUGIN_EXPORT uint32_t PLUGIN_CALLING_CONVENTION deactivateFeatures(uint32_t features);

	
	// -------- Positional Audio --------
	// If this plugin wants to provide positional audio, all functiosn of this category have to be implemented

	/// Indicates that Mumble wants to use this plugin to request positional data. Therefore it should check whether it is currently
	/// able to do so and allocate memory that is needed for that process.
	/// As a parameter this function gets an array of names and an array of PIDs. They are of same length and the PID at index i
	/// belongs to a program whose name is listed at index i in the "name-array".
	///
	/// @param programNames An array of pointers to the program names
	/// @param programPIDs An array of the corresponding program PIDs
	/// @param programCount The length of programNames and programPIDs
	/// @returns The error code. If everything went fine PDEC_OK shall be returned. In that case Mumble will start frequently
	/// 	calling fetchPositionalData. If this returns anything but PDEC_OK, Mumble will assume that the plugin is (currently)
	/// 	uncapable of providing positional data. In this case this function must not have allocated any memory that needs to be
	/// 	cleaned up later on. Depending on the returned error code, Mumble might try to call this function again later on.
	PLUGIN_EXPORT uint8_t PLUGIN_CALLING_CONVENTION initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount);

	/// Retrieves the positional audio data. If no data can be fetched, set all float-vectors to 0 and return false.
	///
	/// @param[out] avatarPos A float-array of size 3 representing the cartesian position of the player/avatar in the ingame world.
	/// 	One unit represents one meter of distance.
	/// @param[out] avatarDir A float-array of size 3 representing the cartesian direction-vector of the player/avatar ingame (where it
	/// 	is facing).
	/// @param[out] avatarAxis A float-array of size 3 representing the vector pointing from the toes of the character to its head. One
	/// 	unit represents one meter of distance.
	/// @param[out] cameraPos A float-array of size 3 representing the cartesian position of the camera in the ingame world.
	/// 	One unit represents one meter of distance.
	/// @param[out] cameraDir A float-array of size 3 representing the cartesian direction-vector of the camera ingame (where it
	/// 	is facing).
	/// @param[out] cameraAxis A float-array of size 3 representing a vector from the bottom of the camera to its top. One unit
	/// 	represents one meter of distance.
	/// @param[out] context A pointer to where the pointer to a C-encoded string storing the context of the provided positional data
	/// 	shall be written. This context should include information about the server (and team) the player is on. Only players with identical
	/// 	context will be able to hear each other's audio. The returned pointer has to remain valid until the next invokation of this function
	/// 	or until shutdownPositionalData is called.
	/// @param[out] identity A pointer to where the pointer to a C-encoded string storing the identity of the player shall be written. It can
	/// 	be polled by external scripts from the server and should uniquely identify the player in the game. The pointer has to remain valid
	/// 	until the next invokation of this function or until shutdownPositionalData is called.
	/// @returns Whether this plugin can continue delivering positional data. If this function returns false, shutdownPositionalData will
	/// 	be called.
	PLUGIN_EXPORT bool PLUGIN_CALLING_CONVENTION fetchPositionalData(float *avatarPos, float *avatarDir, float *avatarAxis, float *cameraPos, float *cameraDir,
			float *cameraAxis, const char **context, const char **identity);

	/// Indicates that this plugin will not be asked for positional data any longer. Thus any memory allocated for this purpose should
	/// be freed at this point.
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION shutdownPositionalData();



	// -------- EventHandlers / Callback functions -----------

	/// Called when connecting to a server.
	///
	/// @param connection The ID of the newly established server-connection
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION onServerConnected(MumbleConnection_t connection);

	/// Called when disconnecting from a server.
	///
	/// @param connection The ID of the server-connection that has been terminated
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION onServerDisconnected(MumbleConnection_t connection);

	/// Called whenever any user on the server enters a channel
	/// This function will also be called when freshly connecting to a server as each user on that
	/// server needs to be "added" to the respective channel as far as the local client is concerned.
	///
	/// @param connection The ID of the server-connection this event is connected to
	/// @param userID The ID of the user this event has been triggered for
	/// @param previousChannelID The ID of the chanel the user is coming from. Negative IDs indicate that there is no previous channel (e.g. the user
	/// 	freshly connected to the server) or the channel isn't available because of any other reason.
	/// @param newChannelID The ID of the channel the user has entered. If the ID is negative, the new channel could not be retrieved. This means
	/// 	that the ID is invalid.
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION onChannelEntered(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t previousChannelID,
			MumbleChannelID_t newChannelID);

	/// Called whenever a user leaves a channel.
	/// This includes a client disconnecting from the server as this will also lead to the user not being in that channel anymore.
	///
	/// @param connection The ID of the server-connection this event is connected to
	/// @param userID The ID of the user that left the channel
	/// @param channelID The ID of the channel the user left. If the ID is negative, the channel could not be retrieved. This means that the ID is
	/// 	invalid.
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION onChannelExited(MumbleConnection_t connection, MumbleUserID_t userID, MumbleChannelID_t channelID);

	/// Called when any user changes his/her talking state.
	///
	/// @param connection The ID of the server-connection this event is connected to
	/// @param userID The ID of the user whose talking state has been changed
	/// @param talkingState The new TalkingState the user has switched to.
	PLUGIN_EXPORT void PLUGIN_CALLING_CONVENTION onUserTalkingStateChanged(MumbleConnection_t connection, MumbleUserID_t userID, TalkingState_t talkingState);

	/// Called whenever there is audio input.
	///
	/// @param inputPCM A pointer to a short-array holding the pulse-code-modulation (PCM) representing the audio input. Its length
	/// 	is sampleCount * channelCount.
	/// @param sampleCount The amount of sample points per channel
	/// @param channelCount The amount of channels in the audio
	/// @param isSpeech A boolean flag indicating whether Mumble considers the input as part of speech (instead of background noise)
	/// @returns Whether this callback has modified the audio input-array
	PLUGIN_EXPORT bool PLUGIN_CALLING_CONVENTION onAudioInput(short *inputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech);

	/// Called whenever Mumble fetches data from an active audio source (could be a voice packet or a playing sample).
	/// The provided audio buffer is the raw buffer without any processing applied to it yet.
	///
	/// @param outputPCM A pointer to a float-array holding the pulse-code-modulation (PCM) representing the audio output. Its length
	/// 	is sampleCount * channelCount.
	/// @param sampleCount The amount of sample points per channel
	/// @param channelCount The amount of channels in the audio
	/// @param isSpeech Whether this audio belongs to a received voice packet (and will thus (most likely) contain speech)
	/// @param userID If isSpeech is true, this contains the ID of the user this voice packet belongs to. If isSpeech is false,
	/// 	the content of this parameter is unspecified and should not be accessed
	/// @returns Whether this callback has modified the audio output-array
	PLUGIN_EXPORT bool PLUGIN_CALLING_CONVENTION onAudioSourceFetched(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);

	/// Called whenever Mumble has processed the data from an active audio source (could be a voice packet or a playing sample).
	/// Processing contains things like applying positional audio effects (if enabled) and/or volume adjustments.
	///
	/// @param outputPCM A pointer to a float-array holding the pulse-code-modulation (PCM) representing the audio output. Its length
	/// 	is sampleCount * channelCount.
	/// @param sampleCount The amount of sample points per channel
	/// @param channelCount The amount of channels in the audio
	/// @param isSpeech Whether this audio belongs to a received voice packet (and will thus (most likely) contain speech)
	/// @param userID If isSpeech is true, this contains the ID of the user this voice packet belongs to. If isSpeech is false,
	/// 	the content of this parameter is unspecified and should not be accessed
	/// @returns Whether this callback has modified the audio output-array
	PLUGIN_EXPORT bool PLUGIN_CALLING_CONVENTION onAudioSourceProcessed(float *outputPCM, uint32_t sampleCount, uint16_t channelCount, bool isSpeech, MumbleUserID_t userID);

	/// Called whenever the fully mixed and processed audio is about to be handed to the audio backend (about to be played).
	/// Note that this happens immediately before Mumble clips the audio buffer.
	///
	/// @param outputPCM A pointer to a float-array holding the pulse-code-modulation (PCM) representing the audio output. Its length
	/// 	is sampleCount * channelCount.
	/// @param sampleCount The amount of sample points per channel
	/// @param channelCount The amount of channels in the audio
	/// @returns Whether this callback has modified the audio output-array
	PLUGIN_EXPORT bool PLUGIN_CALLING_CONVENTION onAudioOutputAboutToPlay(float *outputPCM, uint32_t sampleCount, uint16_t channelCount);

	/// Called whenever data has been received that has been sent by a plugin. This data should only be processed by the
	/// intended plugin. For this reason a dataID is provided that should be used to determine whether the data is intended
	/// for this plugin or not. As soon as the data has been processed, no further plugins will be notified about it.
	///
	/// @param connection The ID of the server-connection the data is coming from
	/// @param sender The ID of the user whose client's plugin has sent the data
	/// @param data The sent data represented as a string
	/// @param dataLength The length of data
	/// @param dataID The ID of this data
	/// @return Whether the given data has been processed by this plugin
	PLUGIN_EXPORT bool PLUGIN_CALLING_CONVENTION onReceiveData(MumbleConnection_t connection, MumbleUserID_t sender, const char *data, size_t dataLenght,
			const char *dataID);


#ifdef __cplusplus
}
#endif 


#endif
