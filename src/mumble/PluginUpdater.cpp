// Copyright 2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PluginUpdater.h"
#include "PluginManager.h"
#include "Log.h"
#ifndef NO_PLUGIN_INSTALLER
	#include "PluginInstaller.h"
#endif

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <QtCore/QHashIterator>
#include <QtCore/QSignalBlocker>
#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtConcurrent>
#include <QNetworkRequest>

#include <algorithm>

// We define a global macro called 'g'. This can lead to issues when included code uses 'g' as a type or parameter name (like protobuf 3.7 does). As such, for now, we have to make this our last include.
#include "Global.h"

PluginUpdater::PluginUpdater(QWidget *parent)
	: QDialog(parent),
	  m_wasInterrupted(false),
	  m_dataMutex(),
	  m_pluginsToUpdate(),
	  m_networkManager(),
	  m_pluginUpdateWidgets() {
	
	QObject::connect(&m_networkManager, &QNetworkAccessManager::finished, this, &PluginUpdater::on_updateDownloaded);
}

PluginUpdater::~PluginUpdater() {
	m_wasInterrupted.store(true);
}

void PluginUpdater::checkForUpdates() {
	// Dispatch a thread in which each plugin can check for updates
	QtConcurrent::run([this]() {
		QMutexLocker lock(&m_dataMutex);

		const QVector<const_plugin_ptr_t> plugins = g.pluginManager->getPlugins();

		for (int i = 0; i < plugins.size(); i++) {
			const_plugin_ptr_t plugin = plugins[i];

			if (plugin->hasUpdate()) {
				QUrl updateURL = plugin->getUpdateDownloadURL();

				if (updateURL.isValid() && !updateURL.isEmpty() && !updateURL.fileName().isEmpty()) {
					UpdatePair pair = { plugin->getID(), updateURL };
					m_pluginsToUpdate << pair;
				}
			}

			// if the update has been asked to be interrupted, exit here
			if (m_wasInterrupted.load()) {
				emit updateInterrupted();
				return;
			}
		}

		if (!m_pluginsToUpdate.isEmpty()) {
			emit updatesAvailable();
		}
	});
}

void PluginUpdater::promptAndUpdate() {
	setupUi(this);
	populateUI();

	setWindowIcon(QIcon(QLatin1String("skin:mumble.svg")));

	QObject::connect(qcbSelectAll, &QCheckBox::stateChanged, this, &PluginUpdater::on_selectAll);
	QObject::connect(this, &QDialog::finished, this, &PluginUpdater::on_finished);

	if (exec() == QDialog::Accepted) {
		update();
	}
}

void PluginUpdater::update() {
	QMutexLocker l(&m_dataMutex);

	for (int i = 0; i < m_pluginsToUpdate.size(); i++) {
		UpdatePair currentPair = m_pluginsToUpdate[i];

		m_networkManager.get(QNetworkRequest(currentPair.updateURL));
	}
}

void PluginUpdater::populateUI() {
	clearUI();

	QMutexLocker l(&m_dataMutex);
	for (int i = 0; i < m_pluginsToUpdate.size(); i++) {
		UpdatePair currentPair = m_pluginsToUpdate[i];
		plugin_id_t pluginID = currentPair.pluginID;

		const_plugin_ptr_t plugin = g.pluginManager->getPlugin(pluginID);

		if (!plugin) {
			continue;
		}

		QCheckBox *checkBox = new QCheckBox(qwContent);
		checkBox->setText(plugin->getName());
		checkBox->setToolTip(plugin->getDescription());

		checkBox->setProperty("pluginID", pluginID);

		QLabel *urlLabel = new QLabel(qwContent);
		urlLabel->setText(currentPair.updateURL.toString());
		urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

		UpdateWidgetPair pair = { checkBox, urlLabel };
		m_pluginUpdateWidgets << pair;

		QObject::connect(checkBox, &QCheckBox::stateChanged, this, &PluginUpdater::on_singleSelectionChanged);
	}

	// sort the plugins alphabetically
	std::sort(m_pluginUpdateWidgets.begin(), m_pluginUpdateWidgets.end(), [](const UpdateWidgetPair &first, const UpdateWidgetPair &second) {
		return first.pluginCheckBox->text().compare(second.pluginCheckBox->text(), Qt::CaseInsensitive) < 0;
	});

	// add the widgets to the layout
	for (int i = 0; i < m_pluginUpdateWidgets.size(); i++) {
		UpdateWidgetPair &currentPair = m_pluginUpdateWidgets[i];

		static_cast<QFormLayout*>(qwContent->layout())->addRow(currentPair.pluginCheckBox, currentPair.urlLabel);
	}
}

void PluginUpdater::clearUI() {
	// There are always as many checkboxes as there are labels
	for (int i = 0; i < m_pluginUpdateWidgets.size(); i++) {
		UpdateWidgetPair &currentPair = m_pluginUpdateWidgets[i];

		qwContent->layout()->removeWidget(currentPair.pluginCheckBox);
		qwContent->layout()->removeWidget(currentPair.urlLabel);

		delete currentPair.pluginCheckBox;
		delete currentPair.urlLabel;
	}
}

void PluginUpdater::on_selectAll(int checkState) {
	// failsafe for partially selected state (shouldn't happen though)
	if (checkState == Qt::PartiallyChecked) {
		checkState = Qt::Unchecked;
	}

	// Select or deselect all plugins
	for (int i = 0; i < m_pluginUpdateWidgets.size(); i++) {
		UpdateWidgetPair &currentPair = m_pluginUpdateWidgets[i];

		currentPair.pluginCheckBox->setCheckState(static_cast<Qt::CheckState>(checkState));
	}
}

void PluginUpdater::on_singleSelectionChanged(int checkState) {
	bool isChecked = checkState == Qt::Checked;

	// Block signals for the selectAll checkBox in order to not trigger its
	// check-logic when changing its check-state here
	const QSignalBlocker blocker(qcbSelectAll);

	if (!isChecked) {
		// If even a single item is unchecked, the selectAll checkbox has to be unchecked
		qcbSelectAll->setCheckState(Qt::Unchecked);
		return;
	}

	// iterate through all checkboxes to see whether we have to toggle the selectAll checkbox
	for (int i = 0; i < m_pluginUpdateWidgets.size(); i++) {
		const UpdateWidgetPair &currentPair = m_pluginUpdateWidgets[i];

		if (!currentPair.pluginCheckBox->isChecked()) {
			// One unchecked checkBox is enough to know that the selectAll
			// CheckBox can't be checked, so we can abort at this point
			return;
		}
	}

	qcbSelectAll->setCheckState(Qt::Checked);
}

void PluginUpdater::on_finished(int result) {
	if (result == QDialog::Accepted) {
		if (qcbSelectAll->isChecked()) {
			// all plugins shall be updated, so we don't have to check them individually
			return;
		}

		QMutexLocker l(&m_dataMutex);

		// The user wants to update the selected plugins only
		// remove the plugins tha shouldn't be updated from m_pluginsToUpdate
		for (int i = 0; i < m_pluginsToUpdate.size(); i++) {
			plugin_id_t id = m_pluginsToUpdate[i].pluginID;

			// find the corresponding checkbox
			bool updateCurrent = false;
			for (int k = 0; k < m_pluginUpdateWidgets.size(); k++) {
				QCheckBox *checkBox = m_pluginUpdateWidgets[k].pluginCheckBox;
				QVariant idVariant = checkBox->property("pluginID");

				if (idVariant.isValid() && static_cast<plugin_id_t>(idVariant.toInt()) == id) {
					updateCurrent = checkBox->isChecked();
					break;
				}
			}

			if (!updateCurrent) {
				// remove this entry from the update-vector
				m_pluginsToUpdate.remove(i);

				// we have modified the vector we are currently iterating over, so we also have to take care of
				// modifying the loop index accordingly. By decrementing it by one, we force the next iteration
				// to run with the same index (which will represent another entry in the vector now though).
				i--;
			}
		}
	} else {
		// Nothing to do as the user doesn't want to update anyways
	}
}

void PluginUpdater::interrupt() {
	m_wasInterrupted.store(true);
}

void PluginUpdater::on_updateDownloaded(QNetworkReply *reply) {
	if (reply) {
		QtConcurrent::run([reply, this]() {
			// Schedule reply for deletion
			reply->deleteLater();

			if (m_wasInterrupted.load()) {
				emit updateInterrupted();
				return;
			}

			// Find the ID of the plugin this update is for by comparing the URLs
			plugin_id_t pluginID;
			bool foundID = false;
			{
				QMutexLocker l(&m_dataMutex);

				for (int i = 0; i < m_pluginsToUpdate.size(); i++) {
					if (m_pluginsToUpdate[i].updateURL == reply->url()) {
						pluginID = m_pluginsToUpdate[i].pluginID;
						foundID = true;

						// remove that entry from the vector as it is being updated right here
						m_pluginsToUpdate.removeAt(i);
						break;
					}
				}
			}

			if (!foundID) {
				// Can't match the URL to a pluginID
				qWarning() << "PluginUpdater: Requested update for plugin from"
					<< reply->url() << "but didn't find corresponding plugin again!";
				return;
			}

			// Now get a handle to that plugin
			const_plugin_ptr_t plugin = g.pluginManager->getPlugin(pluginID);

			if (!plugin) {
				// Can't find plugin with given ID
				qWarning() << "PluginUpdater: Got update for plugin with id"
					<< pluginID << "but it doesn't seem to exist anymore!";
				return;
			}

			// We can start actually checking the reply here
			if (reply->error() != QNetworkReply::NoError) {
				// There was an error during this request. Report it
				Log::logOrDefer(Log::Warning,
						QObject::tr("Unable to download plugin update for \"%1\" from \"%2\" (%3)").arg(
							plugin->getName()).arg(reply->url().toString()).arg(
								QString::fromLatin1(
									QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(reply->error())
								)
							)
						);
				return;
			}

			// Reply seems fine -> write file to disk and fire installer
			QByteArray content = reply->readAll();

			// Write the content to a file in the temp-dir
			if (content.isEmpty()) {
				qWarning() << "PluginUpdater: Update for" << plugin->getName() << "from"
					<< reply->url().toString() << "resulted in no content!";
				return;
			}

			if (reply->url().fileName().isEmpty()) {
				// We don't know how to name the file, if the URL doesn't contain a name
				Log::logOrDefer(Log::Warning, QObject::tr(
						"PluginUpdater: Download URL \"%1\" doesn't contain a filename!"
					).arg(reply->url().toString())
				);
				return;
			}

			QFile file(QDir::temp().filePath(reply->url().fileName()));
			if (!file.open(QIODevice::WriteOnly)) {
				qWarning() << "PluginUpdater: Can't open" << file.fileName() << "for writing!";
				return;
			}

			file.write(content);
			file.close();

#ifndef NO_PLUGIN_INSTALLER
			try {
				// Launch installer
				PluginInstaller installer(file.fileName());
				installer.install();

				Log::logOrDefer(Log::Information, QObject::tr("Successfully updated plugin \"%1\"").arg(plugin->getName()));

				// Make sure Mumble won't use the old version of the plugin
				g.pluginManager->rescanPlugins();
			} catch (const PluginInstallException &e) {
				qWarning() << qUtf8Printable(e.getMessage());
			}
#else
			Log::logOrDefer(Log::Information, QObject::tr("Downloaded update for plugin %1 to \"%2\"").arg(plugin->getName()).arg(file.fileName()));
#endif

			{
				QMutexLocker l(&m_dataMutex);

				if (m_pluginsToUpdate.isEmpty()) {
					emit updatingFinished();
				}
			}
		});
	}
}
