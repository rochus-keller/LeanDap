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

#include "AdapterCore.h"
#include "DapTransport.h"
#include "GdbProcess.h"
#include <QDebug>
#include <QJsonValue>
#include <QJsonObject>
#include <QCoreApplication>
#include <QRegExp>
#include <QStringList>

AdapterCore::AdapterCore(QObject *parent, bool standalone)
    : QObject(parent),m_standalone(standalone),
      m_sequence(1), m_gdbTokenSeq(1),
      m_nextVarRef(65536), m_breakAtStart(false)
{
    m_gdb = new GdbProcess(this);
    connect(m_gdb, SIGNAL(resultRecordReceived(int,QString)), this, SLOT(handleGdbResult(int,QString)));
    connect(m_gdb, SIGNAL(asyncRecordReceived(char,QString)), this, SLOT(handleGdbAsync(char,QString)));
    connect(m_gdb, SIGNAL(targetOutputReceived(QString)), this, SLOT(handleTargetOutput(QString)));
    connect(m_gdb, SIGNAL(consoleStreamReceived(QString)), this, SLOT(handleConsoleStream(QString)));
}

void AdapterCore::start()
{
    qDebug() << "AdapterCore started. Waiting for DAP requests...";
    m_gdb->start();
}

void AdapterCore::sendDapResponse(const QJsonObject& request, bool success, const QJsonObject& body)
{
    QJsonObject response;
    response.insert("type", "response");
    response.insert("seq", m_sequence++);
    response.insert("request_seq", request.value("seq").toInt());
    response.insert("command", request.value("command").toString());
    response.insert("success", success);
    if (!body.isEmpty()) {
        response.insert("body", body);
    }

    emit transmitMessage(response);
}

void AdapterCore::sendDapEvent(const QString& event, const QJsonObject& body)
{
    QJsonObject evt;
    evt.insert("type", "event");
    evt.insert("seq", m_sequence++);
    evt.insert("event", event);
    if (!body.isEmpty()) {
        evt.insert("body", body);
    }

    emit transmitMessage(evt);
}

void AdapterCore::handleDapRequest(const QJsonObject& message)
{
    QString type = message.value("type").toString();
    if (type != "request") return;

    QString command = message.value("command").toString();
    qDebug() << "DAP Request:" << command;

    if (command == "initialize") {
        QJsonObject capabilities;
        capabilities.insert("supportsConfigurationDoneRequest", true);
        sendDapResponse(message, true, capabilities);

        // Trigger the IDE to send breakpoints
        sendDapEvent("initialized");
    }
    else if (command == "launch") {
        processLaunch(message);
    }
    else if (command == "setBreakpoints") {
        processSetBreakpoints(message);
    }
    else if (command == "configurationDone") {
        processConfigurationDone(message);
    }
    else if (command == "continue") {
        processExecCommand(message, "-exec-continue");
    }
    else if (command == "next") {
        processExecCommand(message, "-exec-next");
    }
    else if (command == "stepIn") {
        processExecCommand(message, "-exec-step");
    }
    else if (command == "stepOut") {
        processExecCommand(message, "-exec-finish");
    }
    else if (command == "disconnect") {
        sendDapResponse(message, true);
        if( m_standalone )
            QCoreApplication::quit();
        else
            m_gdb->stop();
    }
    else if (command == "threads") {
        processThreads(message);
    }
    else if (command == "stackTrace") {
        processStackTrace(message);
    }
    else if (command == "scopes") {
        processScopes(message);
    }
    else if (command == "variables") {
        processVariables(message);
    }
    else if (command == "setFunctionBreakpoints") {
        processSetFunctionBreakpoints(message);
    }
    else {
        sendDapResponse(message, true);
    }
}

void AdapterCore::processLaunch(const QJsonObject& message)
{
    QJsonObject args = message.value("arguments").toObject();
    QString program = args.value("program").toString();

    // Check if the IDE requested us to stop at the entry point
    m_breakAtStart = args.value("stopAtEntry").toBool(false);

    // Load the binary, but DO NOT run it yet.
    m_gdb->sendCommand(-1, "-file-exec-and-symbols \"" + program + "\"");

    // Acknowledge launch immediately. The IDE will now send setBreakpoints.
    sendDapResponse(message, true);
}

void AdapterCore::processSetBreakpoints(const QJsonObject& message)
{
    QJsonObject args = message.value("arguments").toObject();
    QJsonObject source = args.value("source").toObject();
    QString path = source.value("path").toString();
    QJsonArray breakpoints = args.value("breakpoints").toArray();

    // In a full implementation, we would send "-break-delete" for old breakpoints here.

    QJsonArray responseBreakpoints;

    for (int i = 0; i < breakpoints.size(); ++i) {
        QJsonObject bp = breakpoints[i].toObject();
        int line = bp.value("line").toInt();

        // Tell GDB to set the breakpoint (e.g. -break-insert /path/to/main.c:15)
        QString miCmd = QString("-break-insert -f %1:%2").arg(path).arg(line);
        m_gdb->sendCommand(-1, miCmd);

        // Tell the IDE the breakpoint was accepted
        QJsonObject respBp;
        respBp.insert("verified", true);
        respBp.insert("line", line);
        responseBreakpoints.append(respBp);
    }

    QJsonObject body;
    body.insert("breakpoints", responseBreakpoints);
    sendDapResponse(message, true, body);
}

void AdapterCore::processConfigurationDone(const QJsonObject& message)
{
    // The IDE is done setting up. Now we run the program
    int token = m_gdbTokenSeq++;
    m_pendingRequests.insert(token, message);

    m_gdb->sendCommand(token, QString("-exec-run %1").arg(m_breakAtStart ? "--start": ""));
}

void AdapterCore::processExecCommand(const QJsonObject& message, const QString& miCommand)
{
    int token = m_gdbTokenSeq++;
    m_pendingRequests.insert(token, message);

    m_gdb->sendCommand(token, miCommand);
}

void AdapterCore::handleGdbResult(int token, const QString& record)
{
    GdbProcess::log("GDB Result [" + QByteArray::number(token) + "] >", record);

    if (record.startsWith("error")) {
        QString errMsg = extractMiValue(record, "msg");

        // Tell the IDE the target is actually stopped due to an error
        QJsonObject body;
        body.insert("reason", "exception"); // "exception" forces IDEs to pay attention
        body.insert("text", errMsg);
        body.insert("threadId", 1);
        body.insert("allThreadsStopped", true);
        sendDapEvent("stopped", body);

        // Print the error to the IDE console in red
        QJsonObject outBody;
        outBody.insert("category", "stderr");
        outBody.insert("output", "GDB Execution Error: " + errMsg + "\n");
        sendDapEvent("output", outBody);
    }

    if (m_pendingRequests.contains(token)) {
        QJsonObject originalRequest = m_pendingRequests.take(token);
        QString command = originalRequest.value("command").toString();

        if (command == "launch" || command == "continue" || command == "next" || command == "stepIn" || command == "stepOut") {
            bool success = record.startsWith("running") || record.startsWith("done");
            sendDapResponse(originalRequest, success);
        }
        else if (command == "stackTrace") {
            QJsonArray stackFrames;

            // GDB outputs: ^done,stack=[frame={level="0",addr="..",func="main",file="t.c",line="4"}]
            // We slice it up using "frame={"
            QStringList framesStr = record.split("frame={");
            for (int i = 1; i < framesStr.size(); ++i) { // Skip index 0 (the "done,stack=[" part)
                QString fStr = framesStr[i];

                QJsonObject frame;
                frame.insert("id", extractMiValue(fStr, "level").toInt());
                frame.insert("name", extractMiValue(fStr, "func"));
                frame.insert("line", extractMiValue(fStr, "line").toInt());
                frame.insert("column", 0);

                QJsonObject source;
                source.insert("name", extractMiValue(fStr, "file"));
                source.insert("path", extractMiValue(fStr, "fullname"));
                frame.insert("source", source);

                stackFrames.append(frame);
            }

            QJsonObject body;
            body.insert("stackFrames", stackFrames);
            body.insert("totalFrames", stackFrames.size());
            sendDapResponse(originalRequest, true, body);
        }
        else if (command == "variables") {
            QJsonArray variables;
            QJsonObject args = originalRequest.value("arguments").toObject();
            int reqRef = args.value("variablesReference").toInt();

            // Parsing Frame Locals
            if (record.contains("variables=[")) {
                QStringList varsStr = record.split("{name=");
                for (int i = 1; i < varsStr.size(); ++i) {
                    QString vStr = "name=" + varsStr[i];

                    QString name = extractMiValue(vStr, "name");
                    QString val = extractMiValue(vStr, "value");
                    QString type = extractMiValue(vStr, "type");

                    int varRef = 0;
                    // Heuristic: If it has a '{' (struct), '*' (pointer), or '[' (array), it is complex!
                    if (val.startsWith("{") || type.contains("*") || type.contains("[") ||
                            type.contains("struct ") || type.contains("class ")) {
                        varRef = m_nextVarRef++;
                        VarNode node;
                        node.expression = name;
                        node.frameId = reqRef - 1000;
                        node.gdbVarObjName = ""; // Empty means we will create it lazily later
                        m_varNodes.insert(varRef, node);
                    }

                    QJsonObject var;
                    var.insert("name", name);
                    var.insert("value", val);
                    var.insert("type", type);
                    var.insert("variablesReference", varRef);
                    variables.append(var);
                }
            }
            // Parsing Complex Struct/Array Children
            else if (record.contains("children=[")) {
                QStringList childrenStr = record.split("child={");
                for (int i = 1; i < childrenStr.size(); ++i) {
                    QString cStr = "child={" + childrenStr[i];

                    QString name = extractMiValue(cStr, "exp"); // DAP 'name' is GDB 'exp'
                    QString val = extractMiValue(cStr, "value");
                    QString type = extractMiValue(cStr, "type");
                    QString gdbName = extractMiValue(cStr, "name");
                    int numchild = extractMiValue(cStr, "numchild").toInt();

                    int varRef = 0;
                    // If the child itself has children (nested struct/pointer)
                    if (numchild > 0 || val.startsWith("{") || type.contains("*")) {
                        varRef = m_nextVarRef++;
                        VarNode node;
                        node.expression = ""; // Not needed, GDB already created it
                        node.frameId = m_varNodes[reqRef].frameId; // Inherit the frame
                        node.gdbVarObjName = gdbName;
                        m_varNodes.insert(varRef, node);

                        if (val.isEmpty() && numchild > 0) val = "{...}";
                    }

                    QJsonObject var;
                    var.insert("name", name);
                    var.insert("value", val);
                    var.insert("type", type);
                    var.insert("variablesReference", varRef);
                    variables.append(var);
                }
            }

            QJsonObject body;
            body.insert("variables", variables);
            sendDapResponse(originalRequest, true, body);
        }
    }
}

void AdapterCore::handleGdbAsync(char type, const QString& record)
{
    GdbProcess::log("GDB Async (" + QByteArray(1,type) + ") >", record);

    if (type == '*' && record.startsWith("stopped")) {

        m_varNodes.clear();
        m_nextVarRef = 65536;

        // Check if the program exited
        if (record.contains("reason=\"exited\"") ||
                record.contains("reason=\"exited-normally\"") ||
                record.contains("reason=\"exited-signalled\""))
        {
            QJsonObject body;
            QString exitCodeStr = extractMiValue(record, "exit-code");

            if (!exitCodeStr.isEmpty()) {
                // "01" will be parsed as 1
                body.insert("exitCode", exitCodeStr.toInt());
            }

            // DAP requires two events for a clean shutdown:
            sendDapEvent("exited", body);       // Tells the IDE the exit code
            sendDapEvent("terminated", QJsonObject()); // Tells the IDE the debug session is over

            return; // Return early so we don't send a "stopped" event
        }

        // Otherwise, the program merely paused (Breakpoint, Step, etc.)
        QJsonObject body;
        if (record.contains("reason=\"breakpoint-hit\"")) {
            body.insert("reason", "breakpoint");
        } else if (record.contains("reason=\"end-stepping-range\"") || record.contains("reason=\"function-finished\"")) {
            body.insert("reason", "step");
        } else {
            body.insert("reason", "pause");
        }

        body.insert("threadId", 1);
        body.insert("allThreadsStopped", true);
        sendDapEvent("stopped", body);
    }
}

QString AdapterCore::extractMiValue(const QString& miStr, const QString& key)
{
    // Looks for key="value" and captures the value
    QRegExp rx(key + "=\"([^\"]*)\"");
    if (rx.indexIn(miStr) != -1) {
        return rx.cap(1);
    }
    return "";
}

void AdapterCore::processThreads(const QJsonObject& message)
{
    // For leandap, we will fake a single running thread to keep it lean.
    QJsonArray threads;
    QJsonObject t;
    t.insert("id", 1);
    t.insert("name", "Main Thread");
    threads.append(t);

    QJsonObject body;
    body.insert("threads", threads);
    sendDapResponse(message, true, body);
}

void AdapterCore::processStackTrace(const QJsonObject& message)
{
    int token = m_gdbTokenSeq++;
    m_pendingRequests.insert(token, message);

    // Ask GDB for the call stack
    m_gdb->sendCommand(token, "-stack-list-frames");
}

void AdapterCore::processScopes(const QJsonObject& message)
{
    // A scope is a container for variables. We will return one "Locals" container.
    QJsonObject args = message.value("arguments").toObject();

    // The IDE asks for a specific frame (e.g., 0 for the top frame)
    int frameId = args.value("frameId").toInt();

    QJsonArray scopes;
    QJsonObject locals;
    locals.insert("name", "Locals");
    locals.insert("presentationHint", "locals");

    // We map Frame 0 -> 1000, Frame 1 -> 1001, etc.
    // We generate a dummy ID based on the frame ID.
    // This exact number is how processVariables knows which frame to switch to later
    locals.insert("variablesReference", 1000 + frameId);
    locals.insert("expensive", false);

    scopes.append(locals);

    QJsonObject body;
    body.insert("scopes", scopes);
    sendDapResponse(message, true, body);
}

void AdapterCore::processVariables(const QJsonObject& message)
{
    QJsonObject args = message.value("arguments").toObject();
    int ref = args.value("variablesReference").toInt();

    int token = m_gdbTokenSeq++;
    m_pendingRequests.insert(token, message);

    // If the reference is between 1000 and 1999, it is a FRAME LOCALS request
    if (ref >= 1000 && ref < 2000) {

        // Extract the frame ID (e.g., 1001 -> Frame 1)
        int frameId = ref - 1000;

        // Tell GDB to switch to that frame. (We send -1 because we don't care about the response).
        m_gdb->sendCommand(-1, QString("-stack-select-frame %1").arg(frameId));

        // Ask GDB for all locals in that frame (2 means "names and values", instead of --simple-values)
        m_gdb->sendCommand(token, "-stack-list-variables 2");
    }
    // If the reference is >= 65536, it is a COMPLEX VARIABLE (struct/array) expansion
    else if (ref >= 65536) {
        // structured variable expansion
        if (!m_varNodes.contains(ref)) {
            sendDapResponse(message, true, QJsonObject());
            return;
        }

        VarNode node = m_varNodes[ref];

        // Switch GDB to the frame where this variable exists
        m_gdb->sendCommand(-1, QString("-stack-select-frame %1").arg(node.frameId));

        // Lazy Creation. If GDB doesn't know about this object yet, create it.
        if (node.gdbVarObjName.isEmpty()) {
            QString varName = QString("var_%1").arg(ref);
            m_gdb->sendCommand(-1, QString("-var-create %1 * %2").arg(varName).arg(node.expression));
            m_varNodes[ref].gdbVarObjName = varName; // Save it
            node.gdbVarObjName = varName;
        }

        // Ask GDB for the struct/array children
        m_gdb->sendCommand(token, QString("-var-list-children --all-values %1").arg(node.gdbVarObjName));
    }
    else {
        // Unknown reference, return empty
        sendDapResponse(message, true, QJsonObject());
    }
}

void AdapterCore::processSetFunctionBreakpoints(const QJsonObject &message)
{
    QJsonObject args = message.value("arguments").toObject();
    QJsonArray breakpoints = args.value("breakpoints").toArray();

    // Note: A full DAP implementation would clear previously set function
    // breakpoints here before setting the new ones.

    QJsonArray responseBreakpoints;

    for (int i = 0; i < breakpoints.size(); ++i) {
        QJsonObject bp = breakpoints[i].toObject();
        QString funcName = bp.value("name").toString();

        // Tell GDB to set the breakpoint by function name
        // -f forces it as a pending breakpoint if the symbol isn't loaded yet
        QString miCmd = QString("-break-insert -f %1").arg(funcName);
        m_gdb->sendCommand(-1, miCmd);

        // Acknowledge to the IDE that the breakpoint was accepted
        QJsonObject respBp;
        respBp.insert("verified", true);
        // Note: We don't know the exact line number until GDB hits it,
        // so we just return verified=true for now.
        responseBreakpoints.append(respBp);
    }

    QJsonObject body;
    body.insert("breakpoints", responseBreakpoints);
    sendDapResponse(message, true, body);
}

void AdapterCore::handleTargetOutput(const QString& text)
{
    QJsonObject body;
    body.insert("category", "stdout");
    body.insert("output", text);

    // Emit the standard DAP output event
    sendDapEvent("output", body);
}

QString AdapterCore::unescapeGdbString(const QString& str)
{
    QString res = str;

    // Remove surrounding quotes if they exist
    if (res.startsWith('"') && res.endsWith('"')) {
        res = res.mid(1, res.length() - 2);
    }

    // Unescape common C-string characters
    res.replace("\\n", "\n");
    res.replace("\\t", "\t");
    res.replace("\\r", "\r");
    res.replace("\\\"", "\"");
    res.replace("\\\\", "\\");

    return res;
}

void AdapterCore::handleConsoleStream(const QString& text)
{
    QJsonObject body;
    body.insert("category", "console");
    body.insert("output", unescapeGdbString(text));

    // Send it through the exact same DAP output event
    sendDapEvent("output", body);
}
