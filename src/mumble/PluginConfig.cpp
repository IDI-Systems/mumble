// Copyright 2005-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PluginConfig.h"

#include "Log.h"
#include "MainWindow.h"
#include "Message.h"
#include "ServerHandler.h"
#include "WebFetch.h"
#include "MumbleApplication.h"
#include "ManualPlugin.h"
#include "Utils.h"
#include "PluginManager.h"

#include <QtCore/QLibrary>
#include <QtCore/QUrlQuery>

#include <QtWidgets/QMessageBox>
#include <QtXml/QDomDocument>

#ifdef Q_OS_WIN
# include <softpub.h>
# include <tlhelp32.h>
#endif

// We define a global macro called 'g'. This can lead to issues when included code uses 'g' as a type or parameter name (like protobuf 3.7 does). As such, for now, we have to make this our last include.
#include "Global.h"

const QString PluginConfig::name = QLatin1String("PluginConfig");

static ConfigWidget *PluginConfigDialogNew(Settings &st) {
	return new PluginConfig(st);
}

static ConfigRegistrar registrar(5000, PluginConfigDialogNew);

struct PluginFetchMeta {
	QString hash;
	QString path;
	
	PluginFetchMeta(const QString &hash_ = QString(), const QString &path_ = QString())
		: hash(hash_)
		, path(path_) { /* Empty */ }
};


PluginConfig::PluginConfig(Settings &st) : ConfigWidget(st) {
	setupUi(this);

	qtwPlugins->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	qtwPlugins->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	qtwPlugins->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	qtwPlugins->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

	refillPluginList();
}

QString PluginConfig::title() const {
	return tr("Plugins");
}

const QString &PluginConfig::getName() const {
	return PluginConfig::name;
}

QIcon PluginConfig::icon() const {
	return QIcon(QLatin1String("skin:config_plugin.png"));
}

void PluginConfig::load(const Settings &r) {
	loadCheckBox(qcbTransmit, r.bTransmitPosition);
}

void PluginConfig::save() const {
	s.bTransmitPosition = qcbTransmit->isChecked();
	s.qhPluginSettings.clear();

	if (!s.bTransmitPosition) {
		// Make sure that if posData is currently running, it gets reset
		// The setting will prevent the system from reactivating
		g.pluginManager->unlinkPositionalData();
	}

	QList<QTreeWidgetItem *> list = qtwPlugins->findItems(QString(), Qt::MatchContains);
	foreach(QTreeWidgetItem *i, list) {
		bool enable = (i->checkState(1) == Qt::Checked);
		bool positionalDataEnabled = (i->checkState(2) == Qt::Checked);
		bool keyboardMonitoringEnabled = (i->checkState(3) == Qt::Checked);

		const_plugin_ptr_t plugin = pluginForItem(i);
		if (plugin) {
			// insert plugin to settings
			g.pluginManager->enablePositionalDataFor(plugin->getID(), positionalDataEnabled);
			g.pluginManager->allowKeyboardMonitoringFor(plugin->getID(), keyboardMonitoringEnabled);

			if (enable) {
				if (g.pluginManager->loadPlugin(plugin->getID())) {
					// potentially deactivate plugin features
					// A plugin's feature is considered to be enabled by default after loading. Thus we only need to
					// deactivate the ones we don't want
					uint32_t featuresToDeactivate = FEATURE_NONE;
					const uint32_t pluginFeatures = plugin->getFeatures();

					if (!positionalDataEnabled && (pluginFeatures & FEATURE_POSITIONAL)) {
						// deactivate this feature only if it is available in the first place
						featuresToDeactivate |= FEATURE_POSITIONAL;
					}

					if (featuresToDeactivate != FEATURE_NONE) {
						uint32_t remainingFeatures = g.pluginManager->deactivateFeaturesFor(plugin->getID(), featuresToDeactivate);

						if (remainingFeatures != FEATURE_NONE) {
							g.l->log(Log::Warning, QString::fromUtf8("Unable to deactivate all requested features for plugin ") + plugin->getName());
						}
					}
				} else {
					// loading failed
					enable = false;
					g.l->log(Log::Warning, QString::fromUtf8("Unable to load plugin ") + plugin->getName());
				}
			} else {
				g.pluginManager->unloadPlugin(plugin->getID());
			}

			s.qhPluginSettings.insert(plugin->getFilePath(), { enable, positionalDataEnabled, keyboardMonitoringEnabled });
		}
	}
}

const_plugin_ptr_t PluginConfig::pluginForItem(QTreeWidgetItem *i) const {
	if (i) {
		return g.pluginManager->getPlugin(i->data(0, Qt::UserRole).toUInt());
	}

	return nullptr;
}

void PluginConfig::on_qpbConfig_clicked() {
	const_plugin_ptr_t plugin = pluginForItem(qtwPlugins->currentItem());

	if (plugin) {
		if (!plugin->showConfigDialog(this)) {
			// if the plugin doesn't support showing such a dialog, we'll show a default one
			QMessageBox::information(this, QLatin1String("Mumble"), tr("Plugin has no configure function."), QMessageBox::Ok, QMessageBox::NoButton);
		}
	}
}

void PluginConfig::on_qpbAbout_clicked() {
	const_plugin_ptr_t plugin = pluginForItem(qtwPlugins->currentItem());

	if (plugin) {
		if (!plugin->showAboutDialog(this)) {
			// if the plugin doesn't support showing such a dialog, we'll show a default one
			QMessageBox::information(this, QLatin1String("Mumble"), tr("Plugin has no about function."), QMessageBox::Ok, QMessageBox::NoButton);
		}
	}
}

void PluginConfig::on_qpbReload_clicked() {
	g.pluginManager->rescanPlugins();
	refillPluginList();
}

void PluginConfig::refillPluginList() {
	qtwPlugins->clear();

	// get plugins already sorted according to their name
	const QVector<const_plugin_ptr_t > plugins = g.pluginManager->getPlugins(true);

	foreach(const_plugin_ptr_t currentPlugin, plugins) {
		QTreeWidgetItem *i = new QTreeWidgetItem(qtwPlugins);
		i->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		i->setCheckState(1, currentPlugin->isLoaded() ? Qt::Checked : Qt::Unchecked);
		
		if (currentPlugin->getFeatures() & FEATURE_POSITIONAL) {
			i->setCheckState(2, currentPlugin->isPositionalDataEnabled() ? Qt::Checked : Qt::Unchecked);
			i->setToolTip(2, QString::fromUtf8("Whether the positional audio feature of this plugin should be enabled"));
		} else {
			i->setToolTip(2, QString::fromUtf8("This plugin does not provide support for positional audio"));
		}

		i->setCheckState(3, currentPlugin->isKeyboardMonitoringAllowed() ? Qt::Checked : Qt::Unchecked);
		i->setToolTip(3, QObject::tr("Whether this plugin has the permission to be listening to all keyboard events that occur while Mumble has focus"));

		i->setText(0, currentPlugin->getName());
		i->setToolTip(0, currentPlugin->getDescription().toHtmlEscaped());
		i->setToolTip(1, QString::fromUtf8("Whether this plugin should be enabled"));
		i->setData(0, Qt::UserRole, currentPlugin->getID());
	}

	qtwPlugins->setCurrentItem(qtwPlugins->topLevelItem(0));
	on_qtwPlugins_currentItemChanged(qtwPlugins->topLevelItem(0), NULL);
}

void PluginConfig::on_qtwPlugins_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *) {
	const_plugin_ptr_t plugin = pluginForItem(current);

	if (plugin) {
		qpbAbout->setEnabled(plugin->providesAboutDialog());

		qpbConfig->setEnabled(plugin->providesConfigDialog());
	} else {
		qpbAbout->setEnabled(false);
		qpbConfig->setEnabled(false);
	}
}
