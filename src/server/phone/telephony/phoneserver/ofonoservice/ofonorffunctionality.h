/****************************************************************************
**
** This file is part of the Qt Extended Opensource Package.
**
** Copyright (C) 2011 Radek Polak.
**
** Contact: Qt Extended Information (info@qtextended.org)
**
** This file may be used under the terms of the GNU General Public License
** version 2.0 as published by the Free Software Foundation and appearing
** in the file LICENSE.GPL included in the packaging of this file.
**
** Please review the following information to ensure GNU General Public
** Licensing requirements will be met:
**     http://www.fsf.org/licensing/licenses/info/GPLv2.html.
**
**
****************************************************************************/

#ifndef OFONOPHONERFFUNCTIONALITY_H
#define OFONOPHONERFFUNCTIONALITY_H

#include <QDebug>
#include <qphonerffunctionality.h>

class OFonoTelephonyService;

class OFonoRfFunctionality : public QPhoneRfFunctionality
{
    Q_OBJECT
public:
    OFonoRfFunctionality( OFonoTelephonyService *service );
    ~OFonoRfFunctionality();

    OFonoTelephonyService *service;
    bool modemAlive;
    QPhoneRfFunctionality::Level reqLevel;      // requested level while device status was not alive

    void deviceStatus(QString status);  // called by ofono telephony service to report device status
    
public slots:
    void forceLevelRequest();
    void setLevel( QPhoneRfFunctionality::Level level );
};

#endif

