#ifndef EXTERNALDAPDEBUGGER_H
#define EXTERNALDAPDEBUGGER_H

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

#include "DapDebuggerBase.h"
#include <QProcess>
#include <QByteArray>

namespace Dap {

    class DebuggerExt : public DebuggerBase
    {
        Q_OBJECT
    public:
        explicit DebuggerExt(QObject *parent = 0);
        ~DebuggerExt();

        bool open(const QString& programPath, const QString& adapterPath = QString());
        void close();
        bool isOpen() const;

    protected:
        void transmitRequest(const QJsonObject& request); // Serializes to string and writes to stdin

    private slots:
        void onReadyReadStandardOutput(); // Parses Content-Length and calls handleIncomingMessage()
        void onReadyReadStandardError();  // Emits sigEvent(LOG_MESSAGE)
        void onProcessError(QProcess::ProcessError error);

    private:
        void processBuffer();

        QProcess* m_process;
        QByteArray m_buffer;
        int m_expectedContentLength;
    };
}
#endif // EXTERNALDAPDEBUGGER_H
