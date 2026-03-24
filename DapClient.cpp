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

#include "DapClient.h"
#include <QJsonDocument>
#include <QDebug>

DapClient::DapClient(QObject *parent)
    : QObject(parent),
      m_expectedContentLength(0),
      m_sequence(1)
{
    m_process = new QProcess(this);

    connect(m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadyReadStandardOutput()));
    connect(m_process, SIGNAL(readyReadStandardError()), this, SLOT(onReadyReadStandardError()));
    connect(m_process, SIGNAL(errorOccurred(QProcess::ProcessError)), this, SLOT(onProcessError(QProcess::ProcessError)));
}

DapClient::~DapClient()
{
    if (m_process->state() == QProcess::Running) {
        m_process->terminate();
        m_process->waitForFinished(2000);
    }
}

void DapClient::startAdapter(const QString& pathToLeandap)
{
    m_buffer.clear();
    m_expectedContentLength = 0;
    
    // Start leandap as a child process
    m_process->start(pathToLeandap, QStringList());
    if (!m_process->waitForStarted()) {
        emit logMessage("IDE Error: Failed to start leandap adapter!");
    } else {
        emit logMessage("IDE: leandap adapter started successfully.");
    }
}

void DapClient::sendRequest(const QString& command, const QJsonObject& arguments)
{
    if (m_process->state() != QProcess::Running) return;

    // Build the DAP JSON
    QJsonObject request;
    request.insert("seq", m_sequence++);
    request.insert("type", "request");
    request.insert("command", command);
    if (!arguments.isEmpty()) {
        request.insert("arguments", arguments);
    }

    QJsonDocument doc(request);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    // Add Content-Length Headers
    QByteArray fullMessage;
    fullMessage.append("Content-Length: ");
    fullMessage.append(QByteArray::number(payload.size()));
    fullMessage.append("\r\n\r\n");
    fullMessage.append(payload);

    // Write directly to leandap's stdin via QProcess
    m_process->write(fullMessage);
}

void DapClient::onReadyReadStandardError()
{
    // leandap sends all its qDebug logs to stderr! Let's show them in the IDE.
    QString logs = QString::fromUtf8(m_process->readAllStandardError());
    emit logMessage(logs.trimmed());
}

void DapClient::onReadyReadStandardOutput()
{
    // Read the incoming DAP messages from leandap's stdout
    m_buffer.append(m_process->readAllStandardOutput());
    processBuffer();
}

void DapClient::processBuffer()
{
    while (true) {
        if (m_expectedContentLength == 0) {
            int headerEndIndex = m_buffer.indexOf("\r\n\r\n");
            if (headerEndIndex == -1) return;

            QByteArray header = m_buffer.left(headerEndIndex);
            int clPos = header.indexOf("Content-Length: ");
            if (clPos != -1) {
                clPos += 16;
                int clEnd = header.indexOf("\r\n", clPos);
                if (clEnd == -1) clEnd = header.length();
                m_expectedContentLength = header.mid(clPos, clEnd - clPos).toInt();
            }
            m_buffer.remove(0, headerEndIndex + 4);
        }

        if (m_expectedContentLength > 0 && m_buffer.size() >= m_expectedContentLength) {
            QByteArray payload = m_buffer.left(m_expectedContentLength);
            m_buffer.remove(0, m_expectedContentLength);
            m_expectedContentLength = 0;

            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject msg = doc.object();
                QString type = msg.value("type").toString();
                
                // Route the message to the IDE UI
                if (type == "event") {
                    emit eventReceived(msg.value("event").toString(), msg.value("body").toObject());
                } else if (type == "response") {
                    emit responseReceived(
                        msg.value("command").toString(),
                        msg.value("success").toBool(),
                        msg.value("body").toObject()
                    );
                }
            }
        } else {
            break;
        }
    }
}

void DapClient::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    emit logMessage("IDE Error: leandap process crashed or failed.");
}
