// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_LEGACY_PLUGIN_H_
#define MUMBLE_MUMBLE_LEGACY_PLUGIN_H_

#include "Plugin.h"
#include "../../plugins/mumble_plugin.h"
#include <string>

class LegacyPlugin : public Plugin {
	friend class Plugin; // needed in order for Plugin::createNew to access LegacyPlugin::doInitialize()
	private:
		Q_OBJECT
		Q_DISABLE_COPY(LegacyPlugin)

		MumbleError_t doInit();
		void doShutdown();
		const char* doGetName();
		const char* doGetDescription();
		Version_t doGetAPIVersion();
		void doRegisterAPIFunctions(const MumbleAPI *api);

	protected:
		char *name;
		char *description;
		char *context;
		char *identity;
		std::wstring oldIdentity;
		MumblePlugin *mumPlug;
		MumblePlugin2 *mumPlug2;
		MumblePluginQt *mumPlugQt;

		virtual void resolveFunctionPointers() Q_DECL_OVERRIDE;
		virtual bool doInitialize() Q_DECL_OVERRIDE;

		LegacyPlugin(QString path, QObject *p = 0);
	public:
		virtual ~LegacyPlugin() Q_DECL_OVERRIDE;

		// functions for direct plugin-interaction
		virtual const char* getName() Q_DECL_OVERRIDE;

		virtual const char* getDescription() Q_DECL_OVERRIDE;
		virtual uint8_t initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount) Q_DECL_OVERRIDE;
		virtual bool fetchPositionalData(float *avatar_pos, float *avatar_front, float *avatar_axis, float *camera_pos, float *camera_front,
				float *camera_axis, const char **context, const char **identity) Q_DECL_OVERRIDE;
		virtual void shutdownPositionalData() Q_DECL_OVERRIDE;
};

#endif
