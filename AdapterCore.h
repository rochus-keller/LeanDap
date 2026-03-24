#ifndef ADAPTERCORE_H
#define ADAPTERCORE_H

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
#include <QMap>
#include <QJsonArray>

class GdbProcess;

struct VarNode {
    QString expression;
    int frameId;
    QString gdbVarObjName;
};

class AdapterCore : public QObject
{
    Q_OBJECT

public:
    explicit AdapterCore(QObject *parent = 0);
    void start();

public slots:
    void handleDapRequest(const QJsonObject& request);

signals:
    void transmitMessage(const QJsonObject& message);

private slots:
    void handleGdbResult(int token, const QString& record);
    void handleGdbAsync(char type, const QString& record);

private:
    void processLaunch(const QJsonObject& message);
    void processSetBreakpoints(const QJsonObject& message);
    void processConfigurationDone(const QJsonObject& message);
    void processExecCommand(const QJsonObject& message, const QString& miCommand);
    void sendDapResponse(const QJsonObject& request, bool success = true, const QJsonObject& body = QJsonObject());
    void sendDapEvent(const QString& event, const QJsonObject& body = QJsonObject());
    void processThreads(const QJsonObject& message);
    void processStackTrace(const QJsonObject& message);
    void processScopes(const QJsonObject& message);
    void processVariables(const QJsonObject& message);

    // extract values from GDB MI strings (e.g. extracting "main" from func="main")
    QString extractMiValue(const QString& miStr, const QString& key);

    GdbProcess* m_gdb;

    int m_sequence;
    int m_gdbTokenSeq;
    int m_nextVarRef;
    QMap<int, VarNode> m_varNodes;

    // GDB Token -> original DAP Request that triggered it
    QMap<int, QJsonObject> m_pendingRequests;
};

#endif // ADAPTERCORE_H
