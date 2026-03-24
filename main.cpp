/*
* Copyright 2026 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the LeanDap repository.
*
* The following is the license that applies to this copy of the
* repository. For a license to use the library under conditions
* other than those described here, please email to me@rochus-keller.ch.
*
* GNU General Public License Usage
* This file may be used under the terms of the GNU General Public
* License (GPL) versions 2.0 or 3.0 as published by the Free Software
* Foundation and appearing in the file LICENSE.GPL included in
* the packaging of this file. Please review the following information
* to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
* http://www.gnu.org/copyleft/gpl.html.
*/

#include <QCoreApplication>
#include <QDebug>
#include <stdio.h>
#include "AdapterCore.h"
#include "DapTransport.h"

// DAP strictly requires stdout for protocol messages.
// We must redirect all Qt logs to stderr.
void stderrLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(type);
    Q_UNUSED(context);
    QByteArray localMsg = msg.toLocal8Bit();
    fprintf(stderr, "[LeanDap log] %s\n", localMsg.constData());
    fflush(stderr);
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrLogHandler);
    QCoreApplication app(argc, argv);

    qDebug() << "LeanDap starting up...";

    // QObject tree manages memory. 'app' is the parent.
    DapTransport* transport = new DapTransport(&app);
    AdapterCore* adapter = new AdapterCore(&app);

    // Stdin JSON -> Adapter Core
    QObject::connect(transport, SIGNAL(messageReceived(QJsonObject)),
                     adapter, SLOT(handleDapRequest(QJsonObject)));

    // Adapter Core JSON -> Stdout
    QObject::connect(adapter, SIGNAL(transmitMessage(QJsonObject)),
                     transport, SLOT(sendMessage(QJsonObject)));

    adapter->start();

    return app.exec();
}
