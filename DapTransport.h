#ifndef DAPTRANSPORT_H
#define DAPTRANSPORT_H

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
#include <QByteArray>
#include <QThread>

// A dedicated background thread to blockingly read stdin safely on all operating systems
class StdinReader : public QThread
{
    Q_OBJECT
signals:
    void dataReceived(const QByteArray& data);
    void eofReceived();

protected:
    void run();
};

class DapTransport : public QObject
{
    Q_OBJECT

public:
    explicit DapTransport(QObject *parent = 0);
    ~DapTransport();

public slots:
    void sendMessage(const QJsonObject& message);

signals:
    void messageReceived(const QJsonObject& message);

private slots:
    void onDataReceived(const QByteArray& data);
    void onEofReceived();

private:
    void processBuffer();

    StdinReader* m_reader;
    QByteArray m_buffer;
    int m_expectedContentLength;
};

#endif // DAPTRANSPORT_H
