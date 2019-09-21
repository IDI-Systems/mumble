// Copyright 2005-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MANUALPLUGIN_H_
#define MUMBLE_MUMBLE_MANUALPLUGIN_H_

#include <QtCore/QtGlobal>
#include <QtWidgets/QDialog>
#include <QtWidgets/QGraphicsItem>
#include <QtWidgets/QGraphicsScene>

#include "ui_ManualPlugin.h"
#include "LegacyPlugin.h"

#define MUMBLE_ALLOW_DEPRECATED_LEGACY_PLUGIN_API
#include "../../plugins/mumble_legacy_plugin.h"

class Manual : public QDialog, public Ui::Manual {
		Q_OBJECT
	public:
		/// Default constructor
		Manual(QWidget *parent = 0);

	public slots:
		void on_qpbUnhinge_pressed();
		void on_qpbLinked_clicked(bool);
		void on_qpbActivated_clicked(bool);
		void on_qdsbX_valueChanged(double);
		void on_qdsbY_valueChanged(double);
		void on_qdsbZ_valueChanged(double);
		void on_qsbAzimuth_valueChanged(int);
		void on_qsbElevation_valueChanged(int);
		void on_qdAzimuth_valueChanged(int);
		void on_qdElevation_valueChanged(int);
		void on_qleContext_editingFinished();
		void on_qleIdentity_editingFinished();
		void on_buttonBox_clicked(QAbstractButton *);
	protected:
		QGraphicsScene *m_qgsScene;
		QGraphicsItem *m_qgiPosition;

		bool eventFilter(QObject *, QEvent *);
		void changeEvent(QEvent *e);
		void updateTopAndFront(int orientation, int azimut);
};

MumblePlugin *ManualPlugin_getMumblePlugin();
MumblePluginQt *ManualPlugin_getMumblePluginQt();


/// A built-in "plugin" for positional data gatherig allowing for manually placing the "players" in a UI
class ManualPlugin : public LegacyPlugin {
	friend class Plugin; // needed in order for Plugin::createNew to access LegacyPlugin::doInitialize()
	private:
		Q_OBJECT
		Q_DISABLE_COPY(ManualPlugin)
	
	protected:
		virtual void resolveFunctionPointers() Q_DECL_OVERRIDE;
		ManualPlugin(QObject *p = nullptr);
	
	public:
		virtual ~ManualPlugin() Q_DECL_OVERRIDE;
};

#endif
