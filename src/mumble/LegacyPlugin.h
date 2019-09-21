// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_LEGACY_PLUGIN_H_
#define MUMBLE_MUMBLE_LEGACY_PLUGIN_H_

#include "Plugin.h"
#include "../../plugins/mumble_plugin.h"

class LegacyPlugin : public Plugin {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(LegacyPlugin)

	protected:
		virtual void resolveFunctionPointers() Q_DECL_OVERRIDE;
		virtual void setFunctionPointersToNull() Q_DECL_OVERRIDE;
		virtual void setDefaultImplementations() Q_DECL_OVERRIDE;

		// inherit constructor
		using Plugin::Plugin;

	public:
		virtual ~LegacyPlugin() Q_DECL_OVERRIDE;

		MumblePlugin *mumPlug;
		MumblePlugin2 *mumPlug2;
		MumblePluginQt *mumPlugQt;

};

#endif
