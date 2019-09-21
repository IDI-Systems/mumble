// Copyright 2019-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PluginInstaller.h"

#include <quazipfile.h>
#include <JlCompress.h>

#include <QtCore/QString>
#include <QtCore/QException>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QDir>

#include <QtGui/QIcon>

#include <exception>
#include <string>

#include "PluginManager.h" // This holds the plugin-path-macro

PluginInstallException::PluginInstallException(const QString& msg)
	: m_msg(msg) {
}

QString PluginInstallException::getMessage() const {
	return m_msg;
}

const QString PluginInstaller::pluginFileExtension = QLatin1String("mumble_plugin");

bool PluginInstaller::canBePluginFile(const QFileInfo& fileInfo) noexcept {
	if (!fileInfo.isFile()) {
		// A plugin file has to be a file (obviously)
		return false;
	}

	if (fileInfo.suffix().compare(PluginInstaller::pluginFileExtension, Qt::CaseInsensitive) == 0
			|| fileInfo.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
		// A plugin file has either the extension given in PluginInstaller::pluginFileExtension or zip
		return true;
	}

	// We might also accept a shared library directly
	return QLibrary::isLibrary(fileInfo.fileName());
}

PluginInstaller::PluginInstaller(const QFileInfo& fileInfo, QWidget *p)
	: QDialog(p),
	  m_pluginArchive(fileInfo),
	  m_plugin(nullptr),
	  m_pluginSource(),
	  m_pluginDestination(),
	  m_copyPlugin(false) {
	setupUi(this);

	setWindowIcon(QIcon(QLatin1String("skin:mumble.svg")));

	QObject::connect(qpbYes, &QPushButton::clicked, this, &PluginInstaller::on_qpbYesClicked);
	QObject::connect(qpbNo, &QPushButton::clicked, this, &PluginInstaller::on_qpbNoClicked);

	init();
}

PluginInstaller::~PluginInstaller() {
	if (m_plugin) {
		delete m_plugin;
	}
}

void PluginInstaller::init() {
	if (!PluginInstaller::canBePluginFile(m_pluginArchive)) {
		throw PluginInstallException(QObject::tr("The file \"%1\" is not a valid plugin file!").arg(m_pluginArchive.fileName()));
	}

	if (QLibrary::isLibrary(m_pluginArchive.fileName())) {
		// For a library the fileInfo provided is already the actual plugin library
		m_pluginSource = m_pluginArchive;

		m_copyPlugin = true;
	} else {
		// We have been provided with a zip-file
		QuaZip pluginZip(m_pluginArchive.fileName());
		pluginZip.setUtf8Enabled(true);

		if (!pluginZip.open(QuaZip::Mode::mdUnzip)) {
			throw PluginInstallException(QObject::tr("Unable to open plugin archive \"%1\"!").arg(m_pluginArchive.fileName()));
		}

		QStringList fileNames = pluginZip.getFileNameList();

		if (fileNames.isEmpty()) {
			throw PluginInstallException(QObject::tr("Plugin archive \"%1\" does not contain any entries!").arg(m_pluginArchive.fileName()));
		}

		QString pluginName;
		foreach(QString currentFileName, fileNames) {
			if (QLibrary::isLibrary(currentFileName)) {
				if (!pluginName.isEmpty()) {
					// There seem to be multiple plugins in here. That's not allowed
					throw PluginInstallException(QObject::tr("Found more than one plugin library for the current OS in \"%1\" (\"%2\" and \"%3\")!").arg(
								m_pluginArchive.fileName()).arg(pluginName).arg(currentFileName));
				}

				pluginName = currentFileName;
			}
		}

		if (pluginName.isEmpty()) {
			throw PluginInstallException(QObject::tr("Unable to find a plugin for this OS in \"%1\"").arg(m_pluginArchive.fileName()));
		}

		// Unpack the plugin library
		QString tmpPluginPath = QDir::temp().filePath(pluginName);
		if (JlCompress::extractFile(pluginZip.getZipName(), pluginName, tmpPluginPath).isEmpty()) {
			throw PluginInstallException(QObject::tr("Unable to extract plugin to \"%1\"").arg(tmpPluginPath));
		}

		m_pluginSource = QFileInfo(tmpPluginPath);
	}

	QString pluginFileName = m_pluginSource.fileName();

	// Try to load the plugin up to see if it is actually valid
	try {
		m_plugin = Plugin::createNew<Plugin>(m_pluginSource.absoluteFilePath());
	} catch(const PluginError&) {
		throw PluginInstallException(QObject::tr("Unable to load plugin \"%1\" - check the plugin interface!").arg(pluginFileName));
	}

	m_pluginDestination = QFileInfo(QString::fromLatin1("%1/%2").arg(static_cast<QString>(PLUGIN_USER_PATH).isEmpty() ?
				QLatin1String(".") : static_cast<QString>(PLUGIN_USER_PATH)).arg(pluginFileName));


	// Now that we located the plugin, it is time to fill in its details in the UI
	qlName->setText(m_plugin->getName());

	version_t pluginVersion = m_plugin->getVersion();
	version_t usedAPIVersion = m_plugin->getAPIVersion();
	qlVersion->setText(QString::fromLatin1("%1 (API %2)").arg(pluginVersion == VERSION_UNKNOWN ?
				QLatin1String("Unknown") : static_cast<QString>(pluginVersion)).arg(usedAPIVersion));

	qlAuthor->setText(m_plugin->getAuthor());

	qlDescription->setText(m_plugin->getDescription());
}

void PluginInstaller::install() const {
	if (!m_plugin) {
		// This function shouldn't even be called, if the pluin obect has not been created...
		throw PluginInstallException(QLatin1String("[INTERNAL ERROR]: Trying to install an invalid plugin"));
	}

	if (m_pluginSource == m_pluginDestination) {
		// Apparently the plugin is already installed
		return;
	}

	if (m_pluginDestination.exists()) {
		// Delete old version first
		if (!QFile(m_pluginDestination.absoluteFilePath()).remove()) {
			throw PluginInstallException(QObject::tr("Unable to delete old plugin at \"%1\"").arg(m_pluginDestination.absoluteFilePath()));
		}
	}

	if (m_copyPlugin) {
		if (!QFile(m_pluginSource.absoluteFilePath()).copy(m_pluginDestination.absoluteFilePath())) {
			throw PluginInstallException(QObject::tr("Unable to copy plugin library from \"%1\" to \"%2\"").arg(m_pluginSource.absoluteFilePath()).arg(
						m_pluginDestination.absoluteFilePath()));
		}
	} else {
		// Move the plugin into the respective dir
		if (!QFile(m_pluginSource.absoluteFilePath()).rename(m_pluginDestination.absoluteFilePath())) {
			throw PluginInstallException(QObject::tr("Unable to move plugin library to \"%1\"").arg(m_pluginDestination.absoluteFilePath()));
		}
	}
}

void PluginInstaller::on_qpbYesClicked() {
	install();

	close();
}

void PluginInstaller::on_qpbNoClicked() {
	close();
}
