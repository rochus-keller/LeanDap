#ifndef DAPDEBUGGERBASE_H
#define DAPDEBUGGERBASE_H

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
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QStringList>

namespace Dap {
    struct DebuggerEvent
    {
        enum EventKind { 
            TARGET_STOPPED = 0, 
            TARGET_RUNNING, 
            TARGET_EXITED, 
            LOG_MESSAGE 
        };

        EventKind kind;
        int threadId;
        QString reason; // "breakpoint", "step", etc.
        QString message; // For logs
    };

    struct Frame
    {
        int id;
        QString function;
        QString file;
        int line;
    };

    struct Variable
    {
        QString name;
        QString type;
        QString value;
        int variablesReference; // >0 means it has children (complex variable)
    };
    
    class DebuggerBase : public QObject
    {
        Q_OBJECT
    public:
        explicit DebuggerBase(QObject *parent = 0);
        virtual ~DebuggerBase();

        virtual bool open(const QString& programPath, const QString& adapterPath = QString()) = 0;
        virtual void close() = 0;

        bool resume();
        bool stepIn(int threadId = 1);
        bool stepOver(int threadId = 1);
        bool stepOut(int threadId = 1);
        bool suspend();

        bool addBreakpoint(const QString& file, int line);
        bool removeBreakpoint(const QString& file, int line);
        bool clearAllBreakpoints();

        QList<int> allThreads();
        QList<Frame> getStack(int threadId = 1);
        QList<Variable> getVariables(int frameId, int variablesReference = 0);

    signals:
        void sigError(const QString& msg);
        void sigEvent(const Dap::DebuggerEvent& event);

    protected slots:
        // subclasses will funnel received JSON responses into this slot
        void handleIncomingMessage(const QJsonObject& msg);

    protected:
        virtual void transmitRequest(const QJsonObject& request) = 0;

        // Shared synchronous waiter
        QJsonObject sendAndWait(const QString& command, const QJsonObject& arguments = QJsonObject());

        int sendRequestAsync(const QString& command, const QJsonObject& arguments = QJsonObject());

        int m_sequence;
        QHash<int, QJsonObject> m_pendingResponses;
        QHash<QString, QSet<int> > m_breakpoints;

    private:
        bool syncBreakpoints(const QString& file);
    };
}
Q_DECLARE_METATYPE(Dap::DebuggerEvent)

#endif // DAPDEBUGGERBASE_H
