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

    log("GDB <", fullCommand);
    QByteArray cmd = fullCommand.toUtf8() + "\n";
    m_process->write(cmd);
}

void GdbProcess::stop()
{
    if( !m_process->isOpen() || m_process->state() == QProcess::NotRunning )
    {
        qDebug() << "GDB already finished!";
        return;
    }

    m_process->terminate();
    if( !m_process->waitForFinished() )
        qDebug() << "Failed to finish GDB!";
}

void GdbProcess::log(const QByteArray& title, const QString &arg)
{
    const int len = 50;
    QByteArray msg = arg.toUtf8();
    msg.replace("\\\"", "\"");
    qDebug() << title.constData() << msg.left(len).constData() << (msg.size() > len ? "..." : "");
}

void GdbProcess::processMiLine(const QString& line)
{
    if (line == "(gdb)")
        return;

    int i = 0;
    while (i < line.length() && line.at(i).isDigit())
        i++;

    // Check if the character after the numbers is a known GDB/MI prefix
    if (i < line.length()) {
        QChar prefix = line.at(i);
        QString payload = line.mid(i + 1);
        int token = (i > 0) ? line.left(i).toInt() : -1;

        switch (prefix.toLatin1()) {
            case '^':
                emit resultRecordReceived(token, payload);
                return; // done
            case '*': case '+': case '=':
                emit asyncRecordReceived(prefix.toLatin1(), payload);
                return;
            case '~':
                return; // ignore: the textual output that GDB would normally print to the screen if a human were typing in the standard GDB CLI.
            case '@': // supposed to be the stdout/stderr of the target application (seems to just include a new line)
                return;
            case '&': // GDB's internal warning and error channel.
                emit consoleStreamReceived(payload);
                return;
        }
    }

    // If we reach this point, the line did not match ANY standard GDB prefix.
    // So we assume it is raw output the target application's printf
    // re-add \n which was removed before
    emit targetOutputReceived(line + "\n");
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

