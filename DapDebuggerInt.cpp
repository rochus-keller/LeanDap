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

#include "DapDebuggerInt.h"
#include "AdapterCore.h"
using namespace Dap;

DebuggerInt::DebuggerInt(QObject *parent)
    : DebuggerBase(parent),
      m_adapter(new AdapterCore(this)) // Instantiate the core in-memory!
{
    // Wire the signals from AdapterCore to our local slots
    connect(m_adapter, SIGNAL(transmitMessage(QJsonObject)), this, SLOT(onAdapterMessage(QJsonObject)));

    connect(m_adapter, SIGNAL(logEmitted(QString)), this, SLOT(onAdapterLog(QString)));
}

DebuggerInt::~DebuggerInt()
{
    close();
}

void DebuggerInt::transmitRequest(const QJsonObject& request)
{
    if (m_adapter) {
        m_adapter->handleDapRequest(request);
    }
}

bool DebuggerInt::open(const QString& programPath, const QString& adapterPath)
{
    Q_UNUSED(adapterPath); // Ignored in integrated mode!

    // Reset base class state
    m_breakpoints.clear();
    m_pendingResponses.clear();
    m_sequence = 1;

    // Tell AdapterCore to start the GDB process
    m_adapter->start();

    // Perform the DAP Handshake (This uses DapDebuggerBase::sendAndWait)
    QJsonObject initRes = sendAndWait("initialize");
    if (initRes.isEmpty() || !initRes.value("success").toBool()) {
        emit sigError("Integrated Debugger: Initialization failed.");
        return false;
    }

    // Launch the target
    QJsonObject launchArgs;
    launchArgs.insert("program", programPath);
    QJsonObject launchRes = sendAndWait("launch", launchArgs);

    if (launchRes.isEmpty() || !launchRes.value("success").toBool()) {
        emit sigError("Integrated Debugger: Failed to launch program.");
        return false;
    }

    // Configuration Done (Tells GDB to execute the binary)
    sendAndWait("configurationDone");

    return true;
}

void DebuggerInt::close()
{
    if (m_adapter) {
        // Use the base class helper to cleanly ask AdapterCore/GDB to shut down
        sendRequestAsync("disconnect", QJsonObject());

    }
}

void DebuggerInt::onAdapterMessage(const QJsonObject& message)
{
    // The adapter has generated a DAP JSON object (Response or Event).
    // We simply hand it directly to the base class routing engine!
    handleIncomingMessage(message);
}

void DebuggerInt::onAdapterLog(const QString& message)
{
    // Route stderr from GDB (or internal adapter logs) to the IDE UI.
    Dap::DebuggerEvent evt;
    evt.kind = Dap::DebuggerEvent::LOG_MESSAGE;
    evt.message = message;

    // Emit the signal defined in DapDebuggerBase
    emit sigEvent(evt);
}
