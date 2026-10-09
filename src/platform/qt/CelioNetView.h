/* Dialog for the built-in Celio net link (see CelioNet.h).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QDialog>
#include <QTimer>

#include <memory>
#include <string>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace QGBA {

class CoreController;
class Window;

class CelioNetView : public QDialog {
Q_OBJECT

public:
	CelioNetView(Window* window, QWidget* parent = nullptr);

	// MGBA_CELIO_NET=create | join:<room> starts the link once the game runs (for scripts and tests)
	static void hookController(std::shared_ptr<CoreController> controller);

private slots:
	void create();
	void join();
	void disconnectLink();
	void copyRoom();
	void refresh();
	void refreshPorts();
	void direct();

private:
	static std::string usbPortFromEnv();
	std::string selectedPort() const;

	Window* m_window;
	QLabel* m_status;
	QLabel* m_room;
	QLineEdit* m_roomInput;
	QPushButton* m_create;
	QPushButton* m_join;
	QPushButton* m_disconnect;
	QPushButton* m_copy;
	QComboBox* m_ports;
	QPushButton* m_refreshPorts;
	QPushButton* m_direct;
	QCheckBox* m_useUsb;
	QTimer m_timer;
};

}
