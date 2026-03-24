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

#include "GdbProcess.h"
#include <QDebug>

GdbProcess::GdbProcess(QObject *parent)
    : QObject(parent)
{
    m_process = new QProcess(this);

    connect(m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadyReadStandardOutput()));
    connect(m_process, SIGNAL(readyReadStandardError()), this, SLOT(onReadyReadStandardError()));
    connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(onProcessFinished(int, QProcess::ExitStatus)));
}

GdbProcess::~GdbProcess()
{
    if (m_process->state() == QProcess::Running) {
        m_process->terminate();
        m_process->waitForFinished(2000);
    }
}

void GdbProcess::start()
{
    QStringList args;
    args << "--interpreter=mi3" << "--quiet"; // Use MI version 3
    
    qDebug() << "Starting GDB...";
    m_process->start("gdb", args);

    if (!m_process->waitForStarted()) {
        qDebug() << "Failed to start GDB!";
    }
}

void GdbProcess::sendCommand(int token, const QString& command)
{
    // Prepend the token to the MI command
    QString fullCommand;
    if (token >= 0) {
        fullCommand = QString::number(token) + command;
    } else {
        fullCommand = command;
    }

    qDebug() << "GDB <" << fullCommand;
    QByteArray cmd = fullCommand.toUtf8() + "\n";
    m_process->write(cmd);
}

void GdbProcess::processMiLine(const QString& line)
{
    if (line == "(gdb)") return;

    // Extract the token if it exists (e.g. "123^done" -> token 123, prefix '^', payload "done")
    int i = 0;
    while (i < line.length() && line.at(i).isDigit()) {
        i++;
    }

    if (i == line.length()) return; // Malformed line

    int token = -1;
    if (i > 0) {
        token = line.left(i).toInt();
    }

    QChar prefix = line.at(i);
    QString payload = line.mid(i + 1);

    switch (prefix.toLatin1()) {
        case '^': // Result record
            emit resultRecordReceived(token, payload);
            break;
        case '*': case '+': case '=': // Async records
            emit asyncRecordReceived(prefix.toLatin1(), payload);
            break;
        case '~': case '@': case '&': // Stream records
            emit consoleStreamReceived(payload);
            break;
        default:
            qDebug() << "Unknown MI Line:" << line;
            break;
    }
}

void GdbProcess::onReadyReadStandardOutput()
{
    // Read all available bytes and append to our string buffer
    QString newData = QString::fromUtf8(m_process->readAllStandardOutput());
    m_outputBuffer += newData;

    // Process line by line
    int newlineIdx;
    while ((newlineIdx = m_outputBuffer.indexOf('\n')) != -1) {
        QString line = m_outputBuffer.left(newlineIdx).trimmed();
        m_outputBuffer.remove(0, newlineIdx + 1);

        if (!line.isEmpty()) {
            processMiLine(line);
        }
    }
}

void GdbProcess::onReadyReadStandardError()
{
    QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    if (!err.isEmpty()) {
        qDebug() << "GDB STDERR:" << err;
    }
}

void GdbProcess::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    qDebug() << "GDB Exited with code" << exitCode;
}

