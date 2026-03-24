#ifndef DAPCLIENT_H
#define DAPCLIENT_H

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

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QByteArray>

class DapClient : public QObject
{
    Q_OBJECT

public:
    explicit DapClient(QObject *parent = 0);
    ~DapClient();

    void startAdapter(const QString& pathToLeandap);

    void sendRequest(const QString& command, const QJsonObject& arguments = QJsonObject());

signals:
    // Emitted when adapter sends an Event (e.g. "stopped", "initialized")
    void eventReceived(const QString& eventName, const QJsonObject& body);
    
    // Emitted when adapter replies to a Request
    void responseReceived(const QString& command, bool success, const QJsonObject& body);
    
    void logMessage(const QString& message);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessError(QProcess::ProcessError error);

private:
    void processBuffer();

    QProcess* m_process;
    QByteArray m_buffer;
    int m_expectedContentLength;
    int m_sequence;
};

#endif // DAPCLIENT_H
