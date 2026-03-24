# Welcome to LeanDap

LeanDap is a lightweight, standalone Debug Adapter Protocol (DAP) to GDB/MI adapter and client library, written entirely in C++98 and Qt 5 (or LeanQt). 

It bridges the gap between modern IDE debugging interfaces (which speak JSON-based DAP) and the GNU Debugger (which speaks the asynchronous, text-based GDB/MI protocol). It includes both the standalone adapter executable (`leandap`) and drop-in Qt client classes (`DapClient` and `DapDebugger`) for embedding a C++ debugger into an IDE.

## Motivation & Pain Points Addressed

Integrating C/C++ debugging into a custom IDE is notoriously difficult. The standard tool, GDB, communicates via the Machine Interface (GDB/MI). However:

1. GDB/MI is cryptic and asynchronous. Commands sent to GDB might not receive a response immediately. Events (like thread creation, breakpoints hitting, or library loading) arrive out of order and require complex state machines and string parsing to handle correctly.
2. Existing DAP Adapters are bloated. Adapters like Microsoft's `cppdbg` or `CodeLLDB` are massive. They often require Node.js, Python, or bleeding-edge C++11/14/17 toolchains to compile and run. 
3. Pipes and Threading Issues: Reading `stdin`/`stdout` pipelines asynchronously across different operating systems without blocking the main thread or using modern `<thread>` primitives is not trivial.

**LeanDap**

* **is native & lean:** it has zero dependencies outside of the core Qt framework. It complies strictly with C++98, meaning it can be compiled on legacy toolchains or embedded systems.
* **abstracts GDB/MI:** it translates standard DAP JSON requests into GDB tokens, handles the asynchronous event queue, and parses MI variable objects into clean tree structures.
* **Synchronous-feeling Client API:** provides a native Qt client class (`DapDebugger`) that allows IDE developers to write linear, blocking-style debugging code without actually freezing the IDE's GUI thread.

## Features Implemented

* **Lifecycle Management:** Start, attach, configure, and safely terminate GDB child processes.
* **Execution Control:** Continue, Step Over (`next`), Step Into (`step`), Step Out (`finish`), and Pause.
* **Breakpoints:** Insert and remove breakpoints by file and line number.
* **Stack Inspection:** Retrieve thread lists and complete call stacks/frames.
* **Local Variables:** Fetch variables for specific stack frames.
* **Complex Variable Expansion:** Lazily evaluates pointers, arrays, structs, and classes using GDB Variable Objects (`-var-create`, `-var-list-children`), allowing users to expand complex memory trees in the IDE.
* **Robust I/O:** Utilizes a dedicated background thread for blocking OS-level pipe reads to prevent `QSocketNotifier` pipe-closure bugs on Windows/macOS.

## Architecture

This project consists of three main ways to interact with the debugger:

1. **`leandap`** (Standalone Adapter Executable): A CLI application that reads DAP JSON from `stdin` and writes DAP JSON to `stdout`. It manages the actual `gdb` process. This executable can be plugged into *any* DAP-compliant IDE (like VS Code).
2. **`DapClient`** (Low-Level IDE Integration): A Qt class that spawns `leandap` as a child process. It exposes a raw asynchronous API where you send and receive Qt JSON objects (Events and Responses).
3. **`DapDebugger`** (High-Level IDE Integration): A synchronous, strongly-typed wrapper around `DapClient`. It completely hides the JSON DAP protocol, providing clean methods like `getStack()` or `stepOver()`, utilizing a local `QEventLoop` to safely wait for GDB responses without locking the UI.

## Building

### Option 1: qmake
Standard Qt build process:
```bash
qmake leandap.pro
make
```

### Option 2 (pending): BUSY & LeanQt
The project will add the [BUSY](https://github.com/rochus-keller/Busy) build system and [LeanQt](https://github.com/rochus-keller/LeanQt). LeanQt provides a stripped-down Qt5 environment perfect for lightweight C++98 applications.


## Usage

### 1. Using `leandap` Standalone (or with VS Code)
Because `leandap` communicates via standard I/O, it acts as a standard DAP server. 
You can test it directly via the command line by piping `Content-Length` formatted JSON to it, or you can point an IDE like Visual Studio Code to the executable:

```json
// launch.json in VS Code (requires a TCP socket wrapper, or use a pipe transport extension)
{
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/my_app",
    "debugServerPath": "/path/to/leandap"
}
```

### 2. Using `DapClient` (Asynchronous JSON API)
If you want to handle the DAP protocol yourself inside your Qt IDE:
```cpp
DapClient* client = new DapClient(this);

connect(client, SIGNAL(eventReceived(QString,QJsonObject)), this, SLOT(onDapEvent(QString,QJsonObject)));
connect(client, SIGNAL(logMessage(QString)), ui->logWindow, SLOT(append(QString)));

client->startAdapter("/path/to/leandap");
client->sendRequest("initialize");
```

### 3. Using `DapDebugger` (Recommended High-Level API)
This is the easiest way to add a debugger to your C++ IDE. It masks the DAP protocol entirely.
There is a DebuggerExt and DebuggerInt version. The former still expects an external leandap executable,
while the latter fully integrates the adapter into the IDE.

```cpp
#include "DapDebugger.h" // Int or Ext suffix

// 1. Setup
Debugger* debugger = new Debugger(this); // again Int or Ext suffix
connect(debugger, SIGNAL(sigEvent(Dap::DebuggerEvent)), this, SLOT(onDebuggerEvent(Dap::DebuggerEvent)));

// 2. Start Debugging
debugger->open("/path/to/my_app", "/path/to/leandap"); // the second argument is only used for DebuggerInt!
debugger->addBreakpoint("main.cpp", 42);

// ... Inside your event handler ...
void MyIDE::onDebuggerEvent(const Dap::DebuggerEvent& event) 
{
    if (event.kind == Dap::DebuggerEvent::TARGET_STOPPED) {
        
        // Fetch stack trace (This safely blocks until GDB replies!)
        QList<Dap::Frame> stack = debugger->getStack(event.threadId);
        
        if (!stack.isEmpty()) {
            // Fetch local variables for the top frame (variablesReference = 0)
            QList<Dap::Variable> locals = debugger->getVariables(stack[0].id, 0);
            
            for (int i = 0; i < locals.size(); ++i) {
                qDebug() << locals[i].name << "=" << locals[i].value;
                
                // If it's a struct/pointer, ask GDB to expand its children
                if (locals[i].variablesReference > 0) {
                    QList<Dap::Variable> children = debugger->getVariables(stack[0].id, locals[i].variablesReference);
                    // ... populate UI tree ...
                }
            }
        }
    }
}
```

## References

* **Debug Adapter Protocol (DAP) Specification:** [https://microsoft.github.io/debug-adapter-protocol/](https://microsoft.github.io/debug-adapter-protocol/)
* **GDB/MI Interface Documentation:** [https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI.html](https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI.html)

## License

This project is licensed under the GNU General Public License (GPL) version 2.0 or 3.0.

This software may be used under the terms of the GNU General Public License (GPL) versions 2.0 or 3.0 as published by the Free Software Foundation and appearing in the file `LICENSE.GPL` included in the packaging of this file. Please review the following information to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html
* http://www.gnu.org/copyleft/gpl.html
