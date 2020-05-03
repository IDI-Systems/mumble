// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_LEGACY_PLUGIN_H_
#define MUMBLE_MUMBLE_LEGACY_PLUGIN_H_

#include "Plugin.h"

#include <QtCore/QString>

#include <string>
#include <memory>

#define MUMBLE_ALLOW_DEPRECATED_LEGACY_PLUGIN_API
#include "mumble_legacy_plugin.h"

class LegacyPlugin;

/// Typedef for a LegacyPlugin pointer
typedef std::shared_ptr<LegacyPlugin> legacy_plugin_ptr_t;
/// Typedef for a const LegacyPlugin pointer
typedef std::shared_ptr<const LegacyPlugin> const_legacy_plugin_ptr_t;

class LegacyPlugin : public Plugin {
	friend class Plugin; // needed in order for Plugin::createNew to access LegacyPlugin::doInitialize()
	private:
		Q_OBJECT
		Q_DISABLE_COPY(LegacyPlugin)

	protected:
		QString m_name;
		QString m_description;
		version_t m_version;
		MumblePlugin *m_mumPlug;
		MumblePlugin2 *m_mumPlug2;
		MumblePluginQt *m_mumPlugQt;

		virtual void resolveFunctionPointers() Q_DECL_OVERRIDE;
		virtual bool doInitialize() Q_DECL_OVERRIDE;

		LegacyPlugin(QString path, bool isBuiltIn = false, QObject *p = 0);

		virtual bool showAboutDialog(QWidget *parent) const Q_DECL_OVERRIDE;
		virtual bool showConfigDialog(QWidget *parent) const Q_DECL_OVERRIDE;
		virtual uint8_t initPositionalData(const char **programNames, const uint64_t *programPIDs, size_t programCount) Q_DECL_OVERRIDE;
		virtual bool fetchPositionalData(Position3D& avatarPos, Vector3D& avatarDir, Vector3D& avatarAxis, Position3D& cameraPos, Vector3D& cameraDir,
				Vector3D& cameraAxis, QString& context, QString& identity) const Q_DECL_OVERRIDE;
		virtual void shutdownPositionalData() Q_DECL_OVERRIDE;
	public:
		virtual ~LegacyPlugin() Q_DECL_OVERRIDE;

		// functions for direct plugin-interaction
		virtual QString getName() const Q_DECL_OVERRIDE;

		virtual QString getDescription() const Q_DECL_OVERRIDE;
		virtual uint32_t getFeatures() const Q_DECL_OVERRIDE;
		virtual version_t getAPIVersion() const Q_DECL_OVERRIDE;

		virtual version_t getVersion() const Q_DECL_OVERRIDE;

		// functions for checking which underlying plugin functions are implemented
		virtual bool providesAboutDialog() const Q_DECL_OVERRIDE;
		virtual bool providesConfigDialog() const Q_DECL_OVERRIDE;
};

#endif
