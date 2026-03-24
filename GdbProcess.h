#ifndef GDBPROCESS_H
#define GDBPROCESS_H

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
#include <QString>

class GdbProcess : public QObject
{
    Q_OBJECT

public:
    explicit GdbProcess(QObject *parent = 0);
    ~GdbProcess();

    void start();
    void sendCommand(int token, const QString& command);

signals:
    // Emitted when GDB outputs stream text (e.g. console prints)
    void consoleStreamReceived(const QString& text);
    
    // Emitted when an asynchronous event happens (e.g. thread created, stopped)
    void asyncRecordReceived(char type, const QString& record);
    
    // Emitted when a command completes (done, error, running)
    void resultRecordReceived(int token, const QString& record);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void processMiLine(const QString& line);

    QProcess* m_process;
    QString m_outputBuffer;
};

#endif // GDBPROCESS_H
