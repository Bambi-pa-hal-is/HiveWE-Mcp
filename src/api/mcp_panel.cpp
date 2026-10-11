#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include "mcp_panel.h"
// After the textual includes, as these import modules
#include "main_window/hivewe.h"
#include "main_window/main_ribbon.h"

namespace api {
	namespace {
		QProcess* server = nullptr;
		/// The end of the server's console output, shown in the dialog
		QString server_output;
		unsigned short editor_api_port = 0;
		bool editor_api_running = false;

		unsigned short mcp_port() {
			return static_cast<unsigned short>(QSettings().value("mcp/port", default_mcp_port).toUInt());
		}

		QString mcp_url() {
			return QString("http://127.0.0.1:%1/mcp").arg(mcp_port());
		}

		QString connect_command() {
			return "claude mcp add --transport http hivewe " + mcp_url();
		}

		/// The configured server, one shipped next to HiveWE in mcp/, or the Release build in a source checkout
		QString find_server() {
			const QString configured = QSettings().value("mcp/serverPath").toString();
			if (!configured.isEmpty() && QFileInfo::exists(configured)) {
				return configured;
			}
			QDir directory(QCoreApplication::applicationDirPath());
			if (QFileInfo::exists(directory.filePath("mcp/HiveWE.Mcp.exe"))) {
				return directory.filePath("mcp/HiveWE.Mcp.exe");
			}
			for (int i = 0; i < 6; i++) {
				const QString candidate = directory.filePath("tools/HiveWE.Mcp/bin/Release/net10.0/HiveWE.Mcp.exe");
				if (QFileInfo::exists(candidate)) {
					return QDir::cleanPath(candidate);
				}
				if (!directory.cdUp()) {
					break;
				}
			}
			return {};
		}

		void stop_server() {
			if (server == nullptr) {
				return;
			}
			server->disconnect();
			server->kill();
			server->waitForFinished(2000);
			delete server;
			server = nullptr;
		}

		/// Returns why the server didn't start, or nothing
		QString start_server() {
			stop_server();
			server_output.clear();
			const QString path = find_server();
			if (path.isEmpty()) {
				return "HiveWE.Mcp.exe was not found. Build tools/HiveWE.Mcp with \"dotnet build -c Release\", or pick the exe with Browse.";
			}

			server = new QProcess();
			server->setProcessChannelMode(QProcess::MergedChannels);
			QObject::connect(server, &QProcess::readyRead, [] {
				server_output = (server_output + QString::fromLocal8Bit(server->readAll())).right(6000);
			});
			const QString api_url = QString("http://127.0.0.1:%1/rpc").arg(editor_api_port);
			server->start(path, {"--http", "--port", QString::number(mcp_port()), "--api-url", api_url});
			if (!server->waitForStarted(5000)) {
				const QString error = server->errorString();
				stop_server();
				return "Starting " + path + " failed: " + error;
			}
			return {};
		}

		QString server_status() {
			if (server == nullptr) {
				return "Not running";
			}
			if (server->state() == QProcess::Running) {
				return "Running, clients connect to " + mcp_url();
			}
			return QString("Stopped (exit code %1). If the port is in use, another HiveWE may already run a server.").arg(server->exitCode());
		}

		QIcon mcp_icon() {
			QPixmap pixmap(64, 64);
			pixmap.fill(Qt::transparent);
			QPainter painter(&pixmap);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(217, 119, 87));
			painter.drawRoundedRect(4, 4, 56, 56, 12, 12);
			QFont font;
			font.setBold(true);
			font.setPixelSize(19);
			painter.setFont(font);
			painter.setPen(Qt::white);
			painter.drawText(pixmap.rect(), Qt::AlignCenter, "MCP");
			return QIcon(pixmap);
		}

		void show_dialog(HiveWE& window) {
			auto* dialog = new QDialog(&window);
			dialog->setAttribute(Qt::WA_DeleteOnClose);
			dialog->setWindowTitle("Connect Claude to HiveWE");
			dialog->setMinimumWidth(620);
			auto* layout = new QVBoxLayout(dialog);

			auto* intro = new QLabel(
				"Claude can build this map through the HiveWE MCP server, which HiveWE starts when it launches.\n"
				"Register it once in Claude Code by running this in a terminal:"
			);
			intro->setWordWrap(true);
			layout->addWidget(intro);

			auto* command_row = new QHBoxLayout;
			auto* command = new QLineEdit(connect_command());
			command->setReadOnly(true);
			auto* copy = new QPushButton("Copy");
			command_row->addWidget(command);
			command_row->addWidget(copy);
			layout->addLayout(command_row);
			QObject::connect(copy, &QPushButton::clicked, [] {
				QApplication::clipboard()->setText(connect_command());
			});

			auto* notes = new QLabel(
				"Then start Claude Code and ask it to work on the open map, e.g. \"make a forest clearing with a river\".\n"
				"Other MCP clients use the URL " + mcp_url() + " (streamable HTTP).\n"
				"Added the stdio version before? Remove it first with: claude mcp remove hivewe"
			);
			notes->setWordWrap(true);
			notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
			layout->addWidget(notes);

			auto* status = new QFormLayout;
			auto* api_status = new QLabel(
				editor_api_running ? QString("Listening on http://127.0.0.1:%1/rpc").arg(editor_api_port)
								   : QString("Not running: port %1 is in use, is another HiveWE open?").arg(editor_api_port)
			);
			auto* mcp_status = new QLabel;
			auto* server_path = new QLabel;
			server_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
			status->addRow("Editor API:", api_status);
			status->addRow("MCP server:", mcp_status);
			status->addRow("Server exe:", server_path);
			layout->addLayout(status);

			auto* output = new QPlainTextEdit;
			output->setReadOnly(true);
			output->setMaximumHeight(110);
			output->setPlaceholderText("Server output");
			layout->addWidget(output);

			auto* autostart = new QCheckBox("Start the MCP server with HiveWE");
			autostart->setChecked(QSettings().value("mcp/autostart", true).toBool());
			QObject::connect(autostart, &QCheckBox::toggled, [](const bool checked) {
				QSettings().setValue("mcp/autostart", checked);
			});
			layout->addWidget(autostart);

			const auto refresh = [mcp_status, server_path, output] {
				mcp_status->setText(server_status());
				const QString path = find_server();
				server_path->setText(path.isEmpty() ? "Not found" : QDir::toNativeSeparators(path));
				if (output->toPlainText() != server_output) {
					output->setPlainText(server_output);
				}
			};
			refresh();
			// Picks up the server exiting or printing while the dialog is open
			auto* timer = new QTimer(dialog);
			QObject::connect(timer, &QTimer::timeout, refresh);
			timer->start(1000);

			auto* buttons = new QHBoxLayout;
			auto* restart = new QPushButton("Start / Restart Server");
			auto* stop = new QPushButton("Stop Server");
			auto* browse = new QPushButton("Browse...");
			auto* close = new QPushButton("Close");
			buttons->addWidget(restart);
			buttons->addWidget(stop);
			buttons->addWidget(browse);
			buttons->addStretch();
			buttons->addWidget(close);
			layout->addLayout(buttons);

			QObject::connect(restart, &QPushButton::clicked, [refresh] {
				const QString error = start_server();
				if (!error.isEmpty()) {
					server_output = error;
				}
				refresh();
			});
			QObject::connect(stop, &QPushButton::clicked, [refresh] {
				stop_server();
				refresh();
			});
			QObject::connect(browse, &QPushButton::clicked, [dialog, refresh] {
				const QString path = QFileDialog::getOpenFileName(dialog, "Select HiveWE.Mcp.exe", find_server(), "HiveWE.Mcp.exe (HiveWE.Mcp.exe)");
				if (!path.isEmpty()) {
					QSettings().setValue("mcp/serverPath", path);
					refresh();
				}
			});
			QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);

			// Not modal, so the editor (and Claude's edits) keep going while it is open
			dialog->show();
		}
	} // namespace

	void install_mcp_panel(HiveWE& window, const unsigned short api_port, const bool api_running, const bool start) {
		editor_api_port = api_port;
		editor_api_running = api_running;

		if (MainRibbon* ribbon = window.findChild<MainRibbon*>()) {
			auto* tab = new QRibbonTab;
			auto* section = new QRibbonSection;
			section->setText("Claude");
			auto* button = new QRibbonButton;
			button->setIcon(mcp_icon());
			button->setText("Connect\nClaude");
			section->addWidget(button);
			tab->addSection(section);
			ribbon->addTab(tab, "MCP");
			QObject::connect(button, &QAbstractButton::clicked, [&window] {
				show_dialog(window);
			});
		}

		if (start && api_running) {
			if (const QString error = start_server(); !error.isEmpty()) {
				server_output = error;
			}
		}
		QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
			stop_server();
		});
	}
} // namespace api
