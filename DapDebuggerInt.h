#ifndef INTEGRATEDDAPDEBUGGER_H
#define INTEGRATEDDAPDEBUGGER_H

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

// Forward declare the adapter core
class AdapterCore;

namespace Dap {

    class DebuggerInt : public DebuggerBase
    {
        Q_OBJECT
    public:
        explicit DebuggerInt(QObject *parent = 0);
        ~DebuggerInt();

        // adapterPath is ignored here
        bool open(const QString& programPath, bool stopAtEntry = false);
        void close();
        bool isOpen() const;

    protected:
        void transmitRequest(const QJsonObject& request); // Calls m_adapter->handleDapRequest() directly

    private slots:
        void onAdapterMessage(const QJsonObject& message); // Just forwards to handleIncomingMessage()
        void onAdapterLog(const QString& message);         // Emits sigEvent(LOG_MESSAGE)

    private:
        AdapterCore* m_adapter;
    };
}
#endif // INTEGRATEDDAPDEBUGGER_H
