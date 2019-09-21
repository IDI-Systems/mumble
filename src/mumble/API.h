// Copyright 2019-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_API_H_
#define MUMBLE_MUMBLE_API_H_

// In here the MumbleAPI struct is defined
#include "MumbleAPI.h"

#include <atomic>

namespace API {
	/// @param apiVersion The API version to get the function struct for
	/// @returns The struct containing the function pointers to the respective API functions
	///
	/// @throws std::invalid_argument if there is no set of API functions for the requested API version
	MumbleAPI getMumbleAPI(const version_t& apiVersion);

	/// Converts from the Qt key-encoding to the API's key encoding.
	///
	/// @param keyCode The Qt key-code that shall be converted
	/// @returns The converted key code or KC_INVALID if conversion failed
	keycode_t qtKeyCodeToAPIKeyCode(unsigned int keyCode);

	/// A class holding non-permanent data set by plugins. Non-permanent means that this data
	/// will not be stored between restarts.
	/// All member field should be atomic in order to be thread-safe
	class PluginData {
		public:
			/// Constructor
			PluginData();
			/// Destructor
			~PluginData();

			/// A flag indicating whether a plugin has requested the microphone to be permanently on (mirroring the
			/// behaviour of the continous transmission mode.
			std::atomic_bool overwriteMicrophoneActivation;

			/// @returns A reference to the PluginData singleton
			static PluginData& get();
	}; // class PluginData
}; // namespace API

#endif
