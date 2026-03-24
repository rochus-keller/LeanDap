QT       += core
QT       -= gui

# Enforce C++98 compliance
CONFIG   += c++98 console
CONFIG   -= app_bundle

TARGET = leandap
TEMPLATE = app

SOURCES += main.cpp \
           AdapterCore.cpp \
           DapTransport.cpp \
           GdbProcess.cpp

HEADERS += AdapterCore.h \
           DapTransport.h \
           GdbProcess.h
