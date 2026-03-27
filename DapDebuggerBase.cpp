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

#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>

using namespace Dap;

DebuggerBase::DebuggerBase(QObject *parent)
    : QObject(parent),
      m_sequence(1)
{
    qRegisterMetaType<Dap::DebuggerEvent>("Dap::DebuggerEvent");
}

DebuggerBase::~DebuggerBase()
{

}

bool DebuggerBase::resume()
{
    QJsonObject res = sendAndWait("continue", QJsonObject{{"threadId", 1}});
    return res.value("success").toBool();
}

bool DebuggerBase::stepIn(int threadId)
{
    QJsonObject res = sendAndWait("stepIn", QJsonObject{{"threadId", threadId}});
    return res.value("success").toBool();
}

bool DebuggerBase::stepOver(int threadId)
{
    QJsonObject res = sendAndWait("next", QJsonObject{{"threadId", threadId}});
    return res.value("success").toBool();
}

bool DebuggerBase::stepOut(int threadId)
{
    QJsonObject res = sendAndWait("stepOut", QJsonObject{{"threadId", threadId}});
    return res.value("success").toBool();
}

bool DebuggerBase::suspend()
{
    QJsonObject res = sendAndWait("pause", QJsonObject{{"threadId", 1}});
    return res.value("success").toBool();
}

bool DebuggerBase::addBreakpoint(const QString& file, int line)
{
     m_breakpoints[file].insert(line);

     if (isOpen()) {
         return syncBreakpoints(file);
     }
     return true; // Saved to memory, will sync on open()
}

bool DebuggerBase::removeBreakpoint(const QString& file, int line)
{
    m_breakpoints[file].remove(line);

    if (isOpen()) {
        return syncBreakpoints(file);
    }
    return true;
}

bool DebuggerBase::addBreakpoint(const QString &functionName)
{
    m_functionBreakpoints.insert(functionName);

    if (isOpen()) {
        return syncFunctionBreakpoints();
    }
    return true; // Saved to memory, will sync on open()
}

bool DebuggerBase::removeBreakpoint(const QString &functionName)
{
    m_functionBreakpoints.remove(functionName);

    if (isOpen()) {
        return syncFunctionBreakpoints();
    }
    return true;
}

bool DebuggerBase::clearAllBreakpoints()
{
    QStringList files = m_breakpoints.keys();
    m_breakpoints.clear();
    bool ok = true;
    if (isOpen()) {
        for (int i = 0; i < files.size(); ++i) {
            ok &= syncBreakpoints(files[i]);
        }
    }
    m_functionBreakpoints.clear();
    if (isOpen()) {
        ok &= syncFunctionBreakpoints();
    }
    return ok;
}

QList<int> DebuggerBase::allThreads()
{
    QList<int> result;
    QJsonObject res = sendAndWait("threads");
    if (res.value("success").toBool()) {
        QJsonArray threads = res.value("body").toObject().value("threads").toArray();
        for (int i = 0; i < threads.size(); ++i) {
            result.append(threads[i].toObject().value("id").toInt());
        }
    }
    return result;
}

QList<Frame> DebuggerBase::getStack(int threadId)
{
    QList<Frame> result;
    QJsonObject res = sendAndWait("stackTrace", QJsonObject{{"threadId", threadId}});
    if (res.value("success").toBool()) {
        QJsonArray frames = res.value("body").toObject().value("stackFrames").toArray();
        for (int i = 0; i < frames.size(); ++i) {
            QJsonObject fObj = frames[i].toObject();
            Frame f;
            f.id = fObj.value("id").toInt();
            f.function = fObj.value("name").toString();
            f.line = fObj.value("line").toInt();
            f.file = fObj.value("source").toObject().value("path").toString();
            result.append(f);
        }
    }
    return result;
}

/* DAP variable hierarchy:
    Thread (e.g., Thread 1)
    Stack Frame (e.g., main() is Frame 0, CalculateFibonacci() is Frame 1)
    Scope (e.g., "Locals", "Globals", "Registers")
    Variable (e.g., int x = 5;)
    Child Variable (e.g., p1.health = 100;)
    */

QList<Variable> DebuggerBase::getVariables(int frameId, int variablesReference)
{
    QList<Variable> result;
    int targetRef = variablesReference;

    // If variablesReference == 0, we need to ask for the scopes of the frame first to get the Locals ID
    if (targetRef == 0) {
        QJsonObject scopesRes = sendAndWait("scopes", QJsonObject{{"frameId", frameId}});
        if (!scopesRes.value("success").toBool())
            return result;

        QJsonArray scopes = scopesRes.value("body").toObject().value("scopes").toArray();
        if (scopes.isEmpty())
            return result;

        targetRef = scopes[0].toObject().value("variablesReference").toInt();
    }

    // Now fetch the actual variables
    QJsonObject varRes = sendAndWait("variables", QJsonObject{{"variablesReference", targetRef}});
    if (varRes.value("success").toBool()) {
        QJsonArray vars = varRes.value("body").toObject().value("variables").toArray();
        for (int i = 0; i < vars.size(); ++i) {
            QJsonObject vObj = vars[i].toObject();
            Variable v;
            v.name = vObj.value("name").toString();
            v.type = vObj.value("type").toString();
            v.value = vObj.value("value").toString();
            v.variablesReference = vObj.value("variablesReference").toInt();
            result.append(v);
        }
    }
    return result;
}

bool DebuggerBase::syncBreakpoints(const QString& file)
{
    QJsonArray bpArray;
    QSet<int> lines = m_breakpoints.value(file);
    foreach (int line, lines) {
        QJsonObject bp;
        bp.insert("line", line);
        bpArray.append(bp);
    }

    QJsonObject args;
    args.insert("source", QJsonObject{{"path", file}});
    args.insert("breakpoints", bpArray);

    QJsonObject res = sendAndWait("setBreakpoints", args);
    return res.value("success").toBool();
}

bool DebuggerBase::syncFunctionBreakpoints()
{
    QJsonArray bpArray;
    foreach (const QString& funcName, m_functionBreakpoints) {
        QJsonObject bp;
        bp.insert("name", funcName);
        bpArray.append(bp);
    }

    QJsonObject args;
    args.insert("breakpoints", bpArray);

    QJsonObject res = sendAndWait("setFunctionBreakpoints", args);
    return res.value("success").toBool();
}

int DebuggerBase::sendRequestAsync(const QString& command, const QJsonObject& arguments)
{
    int seq = m_sequence++;
    QJsonObject request;
    request.insert("seq", seq);
    request.insert("type", "request");
    request.insert("command", command);
    if (!arguments.isEmpty()) {
        request.insert("arguments", arguments);
    }

    // Hand off to the subclass (External or Integrated) to actually send it
    transmitRequest(request);

    return seq;
}


QJsonObject DebuggerBase::sendAndWait(const QString& command, const QJsonObject& arguments)
{
    int seq = sendRequestAsync(command, arguments);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    timer.start(5000); // 5 second timeout

    // This safely pumps the Qt Event loop.
    // External mode will read pipes here. Integrated mode will process signals here.
    while (!m_pendingResponses.contains(seq) && timer.isActive()) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    if (m_pendingResponses.contains(seq)) {
        return m_pendingResponses.take(seq);
    }
    qCritical() << "DebuggerBase::sendAndWait timeout on" << command;
    return QJsonObject(); // Timeout or error
}

void DebuggerBase::handleIncomingMessage(const QJsonObject& msg)
{
    QString type = msg.value("type").toString();

    if (type == "response") {
        int reqSeq = msg.value("request_seq").toInt();
        m_pendingResponses.insert(reqSeq, msg);
    }
    else if (type == "event") {
        QString evtName = msg.value("event").toString();
        DebuggerEvent evt;

        if (evtName == "stopped") {
            evt.kind = DebuggerEvent::Stopped;
            evt.threadId = msg.value("body").toObject().value("threadId").toInt();
            evt.reason = msg.value("body").toObject().value("reason").toString();
            emit sigEvent(evt);
        } else if (evtName == "continued") {
            evt.kind = DebuggerEvent::Continued;
            emit sigEvent(evt);
        } else if (evtName == "terminated") {
            evt.kind = DebuggerEvent::Finished;
            emit sigEvent(evt);
        } else if (evtName == "exited") {
            const int exitCode = msg.value("body").toObject().value("exitCode").toInt();
#if 0
            DebuggerEvent logEvt;
            logEvt.kind = DebuggerEvent::TargetOutput;
            logEvt.message = QString("Target exited with code %1").arg(exitCode);
            emit sigEvent(logEvt);
#else
            evt.kind = DebuggerEvent::Exited;
            evt.reason = QString::number(exitCode);
            emit sigEvent(evt);
#endif
        } else if (evtName == "initialized") {
            evt.kind = DebuggerEvent::Initialized;
            emit sigEvent(evt);
        }else if (evtName == "output") {
#if 0
            evt.kind = DebuggerEvent::TargetOutput;
            evt.message = msg.value("body").toObject().value("output").toString();
            emit sigEvent(evt);
#else
            const QString cat = msg.value("body").toObject().value("category").toString();
            const QString txt = msg.value("body").toObject().value("output").toString();
            if( cat == "stdout" )
                emit sigLog(txt);
            else if ( cat == "console" )
                qDebug() << "GDB console" << txt.trimmed().toUtf8().constData();
#endif
        }
    }
}
