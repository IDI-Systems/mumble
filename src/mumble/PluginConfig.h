// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGINS_H_
#define MUMBLE_MUMBLE_PLUGINS_H_

#include "ConfigDialog.h"
#include "ui_PluginConfig.h"
#include "Plugin.h"

#ifdef Q_OS_WIN
# include "win.h"
#endif

#include <QtCore/QObject>
#include <QtCore/QMutex>
#include <QtCore/QReadWriteLock>
#include <QtCore/QUrl>

struct PluginInfo;

/// A graphical way of configuring plugins
class PluginConfig : public ConfigWidget, public Ui::PluginConfig {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PluginConfig)
	protected:
		/// Clears and (re-) populates the plugin list in the UI with the currently available plugins
		void refillPluginList();
		/// @param item The QTreeWidgetItem to retrieve the plugin for
		/// @returns The plugin corresponding to the provided item
		const_plugin_ptr_t pluginForItem(QTreeWidgetItem *item) const;
	public:
		/// Constructor
		///
		/// @param st The settings object to work on
		PluginConfig(Settings &st);
		/// @returns The title of this widget
		virtual QString title() const Q_DECL_OVERRIDE;
		/// @returns The icon for this widget
		virtual QIcon icon() const Q_DECL_OVERRIDE;
	public slots:
		/// Saves the current configuration to the respective settings object
		void save() const Q_DECL_OVERRIDE;
		/// Loads the transmit-position from the provided settings object
		///
		/// @param The setting sobject to read from
		void load(const Settings &r) Q_DECL_OVERRIDE;
		/// Slot triggered when the config-button in the UI has been clicked
		void on_qpbConfig_clicked();
		/// Slot triggered when the about-button in the UI has been clicked
		void on_qpbAbout_clicked();
		/// Slot triggered when the reload-button in the UI has been clicked
		void on_qpbReload_clicked();
		/// Slot triggered when the selection in the plugin list hast changed
		///
		/// @param current The currently selected item
		/// @param old The previously selected item (if applicable - otherwise NULL/nullptr)
		void on_qtwPlugins_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *old);
};

struct PluginFetchMeta;

#endif
