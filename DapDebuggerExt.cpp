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

#include "DapDebuggerExt.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
using namespace Dap;

DebuggerExt::DebuggerExt(QObject *parent)
    : DebuggerBase(parent),
      m_expectedContentLength(0)
{
    m_process = new QProcess(this);
    connect(m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadyReadStandardOutput()));
    connect(m_process, SIGNAL(readyReadStandardError()), this, SLOT(onReadyReadStandardError()));
    connect(m_process, SIGNAL(errorOccurred(QProcess::ProcessError)), this, SLOT(onProcessError(QProcess::ProcessError)));
}

DebuggerExt::~DebuggerExt()
{
    close();
}

bool DebuggerExt::open(const QString& programPath, const QString& adapterPath, bool stopAtEntry)
{
    m_buffer.clear();
    m_expectedContentLength = 0;
    m_pendingResponses.clear();

    m_process->start(adapterPath, QStringList());
    if (!m_process->waitForStarted(3000)) {
        emit sigError("Failed to start adapter process: " + adapterPath);
        return false;
    }

    // Handshake
    QJsonObject initRes = sendAndWait("initialize");
    if (initRes.isEmpty() || !initRes.value("success").toBool()) return false;

    // Launch
    QJsonObject launchArgs;
    launchArgs.insert("program", programPath);
    launchArgs.insert("stopAtEntry", stopAtEntry); // Tell the adapter to pause
    QJsonObject launchRes = sendAndWait("launch", launchArgs);
    if (launchRes.isEmpty() || !launchRes.value("success").toBool()) return false;

    // Configuration Done (Starts the target)
    sendAndWait("configurationDone");
    return true;
}

void DebuggerExt::close()
{
    if (m_process->state() == QProcess::Running) {
        sendRequestAsync("disconnect"); // Tell adapter to shut down nicely
        if (!m_process->waitForFinished(2000)) {
            m_process->terminate();
        }
    }
}

bool DebuggerExt::isOpen() const
{
    return m_process != 0 && m_process->state() == QProcess::Running;
}

void DebuggerExt::transmitRequest(const QJsonObject& request)
{
    // Serialize to DAP HTTP format and write to pipe
    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    QByteArray fullMessage = "Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload;

    if (m_process && m_process->state() == QProcess::Running) {
        m_process->write(fullMessage);
    }
}

void DebuggerExt::onReadyReadStandardOutput()
{
    m_buffer.append(m_process->readAllStandardOutput());
    processBuffer();
}

void DebuggerExt::processBuffer()
{
    while (true) {
        // Parse the HTTP-style Header to find the Content-Length
        if (m_expectedContentLength == 0) {
            int headerEndIndex = m_buffer.indexOf("\r\n\r\n");

            if (headerEndIndex == -1) {
                // The \r\n\r\n hasn't arrived yet. Break the loop and wait
                // for the next readyReadStandardOutput() signal.
                return;
            }

            // Extract just the header string
            QByteArray header = m_buffer.left(headerEndIndex);
            int clPos = header.indexOf("Content-Length: ");

            if (clPos != -1) {
                clPos += 16; // Move past the string "Content-Length: "
                int clEnd = header.indexOf("\r\n", clPos);
                if (clEnd == -1) {
                    clEnd = header.length();
                }

                // Parse the number (e.g., "135")
                QByteArray lengthStr = header.mid(clPos, clEnd - clPos);
                m_expectedContentLength = lengthStr.toInt();
            }

            // Strip the header AND the 4 bytes of "\r\n\r\n" from the buffer
            m_buffer.remove(0, headerEndIndex + 4);
        }

        // Extract the JSON payload using the length we found
        if (m_expectedContentLength > 0 && m_buffer.size() >= m_expectedContentLength) {

            // We have enough bytes! Slice exactly m_expectedContentLength bytes.
            QByteArray payload = m_buffer.left(m_expectedContentLength);

            // Remove those bytes from the buffer so the next message is at index 0
            m_buffer.remove(0, m_expectedContentLength);

            // Reset the counter so the loop looks for a header again next time
            m_expectedContentLength = 0;

            // Parse the JSON string into Qt Objects
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);

            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {

                // SUCCESS! Hand the fully parsed JSON object to the Base Class.
                // The base class will route it to pendingResponses or emit events.
                handleIncomingMessage(doc.object());

            } else {
                // If this happens, leandap sent malformed JSON
                emit sigError("Failed to parse DAP JSON: " + parseError.errorString());
            }
        } else {
            // We have the header, but the payload is incomplete (e.g., we expect
            // 200 bytes but only have 150). Break the loop and wait for more data.
            break;
        }
    }
}

void DebuggerExt::onReadyReadStandardError()
{
    QString logs = QString::fromUtf8(m_process->readAllStandardError());
    DebuggerEvent evt;
    evt.kind = DebuggerEvent::LOG_MESSAGE;
    evt.message = logs.trimmed();
    emit sigEvent(evt);
}

void DebuggerExt::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    emit sigError("Adapter process crashed or failed to start.");
}
