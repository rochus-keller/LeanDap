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

#include "DapTransport.h"
#include <QJsonDocument>
#include <QDebug>
#include <QCoreApplication>
#include <stdio.h>

#ifdef Q_OS_WIN
#include <io.h>
#define READ_CMD _read
#else
#include <unistd.h>
#define READ_CMD read
#endif

void StdinReader::run()
{
    char chunk[4096];
    while (true) {
        // This is a blocking read. It safely sleeps the thread until data or EOF.
        int bytesRead = READ_CMD(0, chunk, sizeof(chunk));

        if (bytesRead > 0) {
            // Qt handles cross-thread signal queueing automatically for QByteArray
            emit dataReceived(QByteArray(chunk, bytesRead));
        } else {
            // bytesRead == 0 means EOF (pipe closed). bytesRead < 0 means error.
            emit eofReceived();
            break;
        }
    }
}

DapTransport::DapTransport(QObject *parent)
    : QObject(parent),
      m_expectedContentLength(0)
{
    m_reader = new StdinReader();
    // QThread signals must be connected to slots in the main thread
    connect(m_reader, SIGNAL(dataReceived(QByteArray)), this, SLOT(onDataReceived(QByteArray)));
    connect(m_reader, SIGNAL(eofReceived()), this, SLOT(onEofReceived()));

    m_reader->start();
}

DapTransport::~DapTransport()
{
    if (m_reader->isRunning()) {
        m_reader->terminate();
        m_reader->wait();
    }
    delete m_reader;
}

void DapTransport::onDataReceived(const QByteArray& data)
{
    m_buffer.append(data);
    processBuffer();
}

void DapTransport::onEofReceived()
{
    qDebug() << "Stdin closed. Exiting.";
    QCoreApplication::quit();
}

void DapTransport::processBuffer()
{
    while (true) {
        // Parse the header to find Content-Length
        if (m_expectedContentLength == 0) {
            int headerEndIndex = m_buffer.indexOf("\r\n\r\n");
            int headerSize = 4;

            // Fallback for manual terminal testing (\n\n instead of \r\n\r\n)
            if (headerEndIndex == -1) {
                headerEndIndex = m_buffer.indexOf("\n\n");
                headerSize = 2;
                if (headerEndIndex == -1) {
                    return; // Wait for more data
                }
            }

            QByteArray header = m_buffer.left(headerEndIndex);
            int clPos = header.indexOf("Content-Length: ");
            if (clPos != -1) {
                clPos += 16;
                int clEnd = header.indexOf("\r\n", clPos);
                if (clEnd == -1) {
                    clEnd = header.length();
                }
                QByteArray lengthStr = header.mid(clPos, clEnd - clPos);
                m_expectedContentLength = lengthStr.toInt();
            }

            m_buffer.remove(0, headerEndIndex + headerSize);
        }

        // Extract the JSON payload based on Content-Length
        if (m_expectedContentLength > 0 && m_buffer.size() >= m_expectedContentLength) {
            QByteArray payload = m_buffer.left(m_expectedContentLength);
            m_buffer.remove(0, m_expectedContentLength);

            m_expectedContentLength = 0; // Reset

            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);

            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                emit messageReceived(doc.object());
            } else {
                qDebug() << "Failed to parse JSON:" << parseError.errorString();
            }
        } else {
            break; // Wait for more data
        }
    }
}

void DapTransport::sendMessage(const QJsonObject& message)
{
    QJsonDocument doc(message);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QByteArray fullMessage;
    fullMessage.append("Content-Length: ");
    fullMessage.append(QByteArray::number(payload.size()));
    fullMessage.append("\r\n\r\n");
    fullMessage.append(payload);

    fwrite(fullMessage.constData(), 1, fullMessage.size(), stdout);
    fflush(stdout);
}
