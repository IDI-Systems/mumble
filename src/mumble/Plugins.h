// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGINS_H_
#define MUMBLE_MUMBLE_PLUGINS_H_

#include <QtCore/QObject>
#include <QtCore/QMutex>
#include <QtCore/QReadWriteLock>
#include <QtCore/QUrl>
#include <QtCore/QSharedPointer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "ConfigDialog.h"
#include "Plugin.h"

#include "ui_Plugins.h"

struct PluginInfo;

class PluginConfig : public ConfigWidget, public Ui::PluginConfig {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PluginConfig)
	protected:
		void refillPluginList();
		const QSharedPointer<const Plugin> pluginForItem(QTreeWidgetItem *) const;
	public:
		PluginConfig(Settings &st);
		virtual QString title() const Q_DECL_OVERRIDE;
		virtual QIcon icon() const Q_DECL_OVERRIDE;
	public slots:
		void save() const Q_DECL_OVERRIDE;
		void load(const Settings &r) Q_DECL_OVERRIDE;
		void on_qpbConfig_clicked();
		void on_qpbAbout_clicked();
		void on_qpbReload_clicked();
		void on_qtwPlugins_currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *);
};

struct PluginFetchMeta;

#endif
