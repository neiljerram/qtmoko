/****************************************************************************
**
** This file is part of the Qt Extended Opensource Package.
**
** Copyright (C) 2011 Radek Polak <psonek2@seznam.cz>
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

#include "vendor_neo_p.h"
#include <qmodemindicators.h>
#include <qatutils.h>
#include <qatresultparser.h>
#include <qmodemllindicators.h>
#include <QProcess>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QSettings>

#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>
#include <alsa/asoundlib.h>

#include <qmodemcallvolume.h>
#include <qmodemsiminfo.h>
#include <qmodemcellbroadcast.h>

#define CLCC_POLL_INTERVAL 1000

// Call provider for GTM601 modem. Currently there is no way how to detect
// remote party hangup so during calls we do CLCC polling to detect it.
NeoCallProvider::NeoCallProvider(NeoModemService * service)
:  QModemCallProvider(service)
    , clccTimer(this)
{
    modemService = service;
    setUseMissedTimer(false);
    setUseDetectTimer(false);

    clccTimer.setSingleShot(true);
    connect(&clccTimer, SIGNAL(timeout()), this, SLOT(doClcc()));
}

NeoCallProvider::~NeoCallProvider()
{
}

bool NeoCallProvider::hasRepeatingRings() const
{
    return true;
}

void NeoCallProvider::ringing(const QString &, const QString &, uint)
{
    llIndicatorsRinging();      // turn on green led
    doClcc();                   // start CLCC polling
}

void NeoCallProvider::clcc(bool, const QAtResult & result)
{
    QModemCall *ic = incomingCall();
    bool icMissing = true;

    int count = 0;

    // Find the current state of the call in the AT+CLCC results.
    QAtResultParser parser(result);
    while (parser.next("+CLCC:")) {
        uint id = parser.readNumeric();
        parser.readNumeric();   // dir
        uint state = parser.readNumeric();
        uint mode = parser.readNumeric();
        parser.readNumeric();   // mpty
        QString number = QAtUtils::decodeNumber(parser);
        QString callType = resolveCallMode(mode);
        //qDebug() << "=============== CLCC id=" << id << ", state =" << state <<
        //    ", mode=" << mode << ", number=" << number << "callType=" << callType;

        if (state == 4) {       // incoming
            if (ic && ic->number() == number) {
                icMissing = false;  // still ringing
            } else {            // new incoming call
                QModemCallProvider::ringing(number, callType, id);
                announceCall();
                clccTimer.start(CLCC_POLL_INTERVAL);
                return;
            }
        }
        count++;
    }

    // We still have call in incoming state
    if (ic) {
        if (icMissing) {
            qLog(Modem) << "Reporting missed call.";    // but it's missing in CLCC list
            ic->setState(QPhoneCall::Missed);   // make the call missed
            return;
        }
        clccTimer.start(CLCC_POLL_INTERVAL);    // continue with polling
        return;
    }
    // We still have some connected calls
    if (count > 0) {
        clccTimer.start(CLCC_POLL_INTERVAL);    // continue with polling
        return;
    }

    qLog(Modem) << "No more calls left";

    QList < QPhoneCallImpl * >list = calls();
    QList < QPhoneCallImpl * >::ConstIterator it;
    for (it = list.begin(); it != list.end(); ++it) {
        (*it)->setState(QPhoneCall::HangupRemote);
    }
}

void NeoCallProvider::doClcc()
{
    if (clccTimer.isActive()) {
        return;                 // CLCC is already running
    }
    atchat()->chat("AT+CLCC", this, SLOT(clcc(bool, QAtResult)));
}

NeoModemService::NeoModemService
    (const QString & service, QSerialIODeviceMultiplexer * mux,
     QObject * parent)
:  QModemService(service, mux, parent)
{
    qDebug() << "Gta04ModemService::constructor";

    // Turn on dynamic signal quality notifications.
    // Register for "_OSIGQ" notifications to get signal quality updates.
    primaryAtChat()->registerNotificationType
        ("_OSIGQ:", this, SLOT(sigq(QString)));

    chat("AT_OSQI=1");          // unsolicited reporting of antenna signal strength, e.g. "_OSIGQ: 3,0"
    chat("AT_OPCMENABLE=1");    // enable the PCM interface for voice calls
    chat("AT_OPSYS=0,2");       // disable UMTS, use only GSM
}

NeoModemService::~NeoModemService()
{
}

void NeoModemService::initialize()
{
    // Disable cell broadcast, GTA04 modem probably does not know AT+CSCB commands
    suppressInterface < QCellBroadcast > ();

    if (!callProvider()) {
        setCallProvider(new NeoCallProvider(this));
    }

    if (!supports < QVibrateAccessory > ())
        addInterface(new NeoVibrateAccessory(this));

    QModemService::initialize();

    // Disable signal and battery polling, just do initial signal check
    indicators()->setPolling(false, true, false);
}

void NeoModemService::sigq(const QString & msg)
{
    // Automatic signal quality update, in the range 0-31, e.g. _OSIGQ: 7,0
    uint posn = 8;
    uint rssi = QAtUtils::parseNumber(msg, posn);
    indicators()->setSignalQuality((int)rssi, 31);
}

void NeoModemService::reset()
{
    qLog(Modem) << "Gta04ModemService::reset()";
}

void NeoModemService::suspend()
{
    qLog(Modem) << " Gta04ModemService::suspend()";
    //chat("AT_OSQI=0");          // unsolicited reporting of antenna signal strength, e.g. "_OSIGQ: 3,0"

    primaryAtChat()->suspend();
    QSerialIODevice *port = multiplexer()->channel("primary");
    port->close();

    suspendDone();
}

void NeoModemService::wake()
{
    qLog(Modem) << " Gta04ModemService::wake()";

    QSerialIODevice *port = multiplexer()->channel("primary");
    port->open(QIODevice::ReadWrite);
    primaryAtChat()->resume();

    post( "modemresumed" );

    //primaryAtChat()->resume();
    //chat("AT_OSQI=1");          // unsolicited reporting of antenna signal strength, e.g. "_OSIGQ: 3,0"
    wakeDone();
}

bool NeoModemService::supportsAtCced()
{
    return false;
}

// Each char of output operator name is 4 chars in input name. The 4 chars is
// integer string of unicode value. E.g.
// 0056006f006400610066006f006e006500200043005a -> Vodafone CZ
QString NeoModemService::decodeOperatorName(QString name)
{
    QString str;
    str.resize(name.size() / 4);
    for (int i = 0; i < str.size(); i++) {
        QString numStr = name.mid(i * 4, 4);
        bool ok;
        int num = numStr.toInt(&ok, 16);
        str[i] = QChar(num);
    }
    return str;
}

// Open GTA04 vibrate device
static int openRumble()
{
    int fd = open("/dev/input/rumble", O_RDWR);
    if (fd > 0)
        return fd;

    perror("rumble open");
    return -1;
}

// Upload vibrate effect, returns effect id or -1 on error
static qint16 uploadEffect(int fd, quint16 strength, int timeoutMs)
{
    struct ff_effect e;

    // Define the new event
    e.type = FF_RUMBLE;
    e.id = -1;
    e.direction = 0;
    e.trigger.button = 0;
    e.trigger.interval = 0;
    e.replay.length = timeoutMs ? timeoutMs : 3000;
    e.replay.delay = 0;
    e.u.rumble.strong_magnitude = strength;
    e.u.rumble.weak_magnitude = 0;

    // Write the event
    if (ioctl(fd, EVIOCSFF, &e) == -1) {
        perror("rumble upload");
        return -1;
    }
    return e.id;
}

static void removeEffect(int &fd, qint16 & effectId, bool closeFd)
{
    if (fd < 0)
        return;

    if (effectId >= 0 && ioctl(fd, EVIOCRMFF, effectId) == -1)
        perror("rumble remove");
    effectId = -1;

    if (closeFd) {
        close(fd);
        fd = -1;
    }
}

NeoVibrateAccessory::NeoVibrateAccessory(QModemService * service)
:  QVibrateAccessoryProvider(service->service(), service)
    , rumbleFd(-1)
    , effectId(-1)
{
    setSupportsVibrateOnRing(true);
    setSupportsVibrateNow(true);
}

NeoVibrateAccessory::~NeoVibrateAccessory()
{
    removeEffect(rumbleFd, effectId, true);
}

void NeoVibrateAccessory::setVibrateOnRing(const bool value)
{
    qLog(Modem) << "setVibrateOnRing " << value;
    setVibrateNow(value);
}

void NeoVibrateAccessory::setVibrateNow(const bool value, int timeoutMs, int strength)
{
    struct input_event event;

    //qLog(Modem) << "setVibrateNow " << value << ", timeoutMs=" << timeoutMs << ", strength=" << strength;

    if (value && rumbleFd < 0)
        rumbleFd = openRumble();

    if (rumbleFd < 0)
        return;

    if (value && effectId < 0)
        effectId = uploadEffect(rumbleFd, (quint16)(strength), timeoutMs);

    if (effectId < 0)
        return;

    // Play/stop the effect
    event.type = EV_FF;
    event.code = effectId;
    event.value = (value ? 1 : 0);

    if (write(rumbleFd, (const void *)&event, sizeof(event)) == -1) {
        perror("rumble write");
    }

    if (!value)
        removeEffect(rumbleFd, effectId, true);

    QVibrateAccessoryProvider::setVibrateNow(value);
}
