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

#include "neoaudioplugin.h"

#include <QDir>
#include <QDebug>
#include <Qtopia>
#include <QProcess>
#include <QAudioState>
#include <QAudioStateInfo>
#include <QValueSpaceItem>
#include <QtopiaIpcAdaptor>
#include <QtopiaIpcEnvelope>
#include <QAudioMixer>
#include <QAudioElement>

#include <qplugin.h>
#include <qtopialog.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef QTOPIA_BLUETOOTH
#include <QBluetoothAudioGateway>
#endif

// For GTA04 audio system docs please check:
//     http://projects.goldelico.com/p/gta04-kernel/page/Sound/
//
// We support following scenarios:
//       Speaker: stereo output from internal GTA04 speaker + internal MIC on phone call
//      Earpiece: default for GSM calls, ouput from earpiece + internal MIC
//       Headset: external headphones
//     Recording: for recording audio e.g. in voice notes app
//  GSMBluetooth: for gsm calls over bluetooth
//
// It seems that restoring whole state is quite fast on GTA04 (200ms). You can
// find in history version based on amixer and switching just needed controls
// but we switch whole state files now because it's more easily customizable
// for user.

/*
static bool amixerSet(QStringList & args)
{
    qLog(AudioState) << "amixer set " << args;

    args.insert(0, "-q");       // make it quiet
    args.insert(0, "set");
    int ret = QProcess::execute("amixer", args);
    if (ret != 0) {
        qWarning() << "amixer returned " << ret;
    }
    return ret == 0;
}*/

bool gta04a3;

#ifdef USE_STATE_FILES

bool usePulse;

static bool alsactl(QStringList & args)
{
    qLog(AudioState) << "alsactl " << args;

    QString cmd = "alsactl";
    if (usePulse) {
        args.insert(0, cmd);
	args.insert(0, "--");
	cmd = "pasuspender";
    }

    for(int i = 0; i < 8; i++) {
        
        QProcess p;

	qLog(AudioState) << cmd << args;
	p.start(cmd, args);
        p.waitForFinished(-1);

	// We need to determine if we're happy with alsactl's output,
	// or if it indicates a problem.  Initially assume that it
	// will be OK.
	bool alsactl_ok = true;

	// We don't expect anything at all on alsactl's standard
	// output.
        QString output = p.readAllStandardOutput();
        if (output.length() != 0) {
	    qWarning() << "alsactl stdout (not expected): " << output;
	    alsactl_ok = false;
	}

	// Some standard error output is expected, and benign.  To
	// process it correctly we need to process the standard error
	// output line by line.
        output = p.readAllStandardError();
	QStringList stderr_lines =
	    QString(output).split("\n", QString::SkipEmptyParts);
	for (int j = 0; j < stderr_lines.size(); j++) {
	    if ((gta04a3) &&
		stderr_lines.at(i).contains("Cannot write control"
					    " '2:0:0:Codec Operation Mode:0'"
					    " : Device or resource busy")) {
		qLog() << "alsactl stderr (ok): " << stderr_lines.at(i);
	    } else if (usePulse &&
		stderr_lines.at(i).contains("XOpenDisplay() failed")) {
		qLog() << "alsactl stderr (ok): " << stderr_lines.at(i);
	    } else {
		qWarning() << "alsactl stderr (not expected): " <<
		                                     stderr_lines.at(i);
		alsactl_ok = false;
	    }
	}

	// If all the output was OK, return successfully.
        if (alsactl_ok)
            return true;

	// Otherwise kill all sound card users, and loop round to try
	// running alsactl again.
        qWarning() <<
	    "alsactl had unexpected output, running kill-snd-card-users.sh";
        QProcess::execute("kill-snd-card-users.sh");
    }
    return false;
}

#endif

static bool writeToFile(const char *filename, const char *val, int len)
{
    qLog(AudioState) << "echo " << val << " > " << filename;
    return (bool) Qtopia::writeFile(filename, val, len);
}

// Run a command and return the first line of its output.
static QString backtick(QString cmd)
{
  QProcess p;

  qLog(AudioState) << cmd;

  // Run the command and wait for it to finish.
  p.start(cmd);
  p.waitForFinished();

  // Get its standard output.
  QString output = p.readAllStandardOutput();

  qLog(AudioState) << "=>" << output;

  // Return the first line of the output.
  return output.split("\n").at(0);
}

QString moduleIdGsmToEar;
QString moduleIdMicToGsm;

static bool gsmVoiceStop()
{
    if (!moduleIdGsmToEar.isEmpty()) {

	// Stop loopback from the modem to the earpiece.
	backtick(QString("pactl unload-module %1").arg(moduleIdGsmToEar));
	moduleIdGsmToEar.clear();

	// Stop loopback from the microphone to the modem.
	backtick(QString("pactl unload-module %1").arg(moduleIdMicToGsm));
	moduleIdMicToGsm.clear();
    }

    // Move back alsa config (used e.g. by blueooth a2dp sound)
    if(QFile::exists("/home/root/.asoundrc.tmp")) {
        QFile::rename("/home/root/.asoundrc.tmp", "/home/root/.asoundrc");
    }

    return true;
}

static bool gsmVoiceStart()
{
    // Move away alsa config (used e.g. by blueooth a2dp sound)
    if(QFile::exists("/home/root/.asoundrc")) {
        QFile::rename("/home/root/.asoundrc", "/home/root/.asoundrc.tmp");
    }

    if (moduleIdGsmToEar.isEmpty()) {

	// Start loopback from the modem to the earpiece.
	moduleIdGsmToEar = backtick("/opt/qtmoko/bin/paLoopGsmToEar");

	// Start loopback from the microphone to the modem.
	moduleIdMicToGsm = backtick("/opt/qtmoko/bin/paLoopMicToGsm");
    }

    return true;
}

/* Class for an audio state based on an ALSA state file. */
class StateFileAudioState : public QAudioState
{
    Q_OBJECT
public:
    StateFileAudioState(QByteArray domain,
			QByteArray profile,
			int priority,
			NeoAudioPlugin *parent);

    virtual QAudioStateInfo info() const;
    virtual QAudio::AudioCapabilities capabilities() const;

    virtual bool isAvailable() const;
    virtual bool enter(QAudio::AudioCapability capability);
    virtual bool leave();

private:
    bool allow_input;
    bool allow_output;
    QAudioStateInfo m_info;
    NeoAudioPlugin *m_parent;
};

StateFileAudioState::StateFileAudioState(QByteArray domain,
					 QByteArray profile,
					 int priority,
					 NeoAudioPlugin *parent) :
    QAudioState(parent)
{
    m_parent = parent;

    m_info.setDomain(domain);
    m_info.setProfile(profile);
    m_info.setPriority(priority);
    m_info.setDisplayName(tr(profile));

    allow_input = false;
    allow_output = false;

    if (m_info.domain() == "Phone") {
	/* All Phone states do both input and output. */
	allow_input = true;
	allow_output = true;
    }
    else if (m_info.profile() == "Recording") {
	/* Recording states do input only. */
	allow_input = true;
    }
    else if (m_info.domain() == "Media") {
	/* All other Media states do output only. */
	allow_output = true;
    }

    /* Note that there is a Null state that allows neither input nor
       output. */
}

QAudioStateInfo StateFileAudioState::info() const
{
    return m_info;
}

QAudio::AudioCapabilities StateFileAudioState::capabilities() const
{
    if (allow_input && allow_output) {
	return (QAudio::InputOnly |
		QAudio::OutputOnly |
		QAudio::InputAndOutput);
    }
    else if (allow_input) {
	return (QAudio::InputOnly);
    }
    else {
	return (QAudio::OutputOnly);
    }
}

bool StateFileAudioState::isAvailable() const
{
    return true;
}

bool StateFileAudioState::enter(QAudio::AudioCapability)
{
    if (gta04a3 && (m_info.domain() != "Phone")) {
        gsmVoiceStop();
    }

#ifdef USE_STATE_FILES

    QString stateFile = "/opt/qtmoko/etc/alsa/";

    if (gta04a3) {
        stateFile += "a3/";
    } else {
        stateFile += "a4/";
    }

    stateFile += m_info.domain() + m_info.profile() + ".state";

    bool ok = alsactl(QStringList() << "-f" << stateFile << "restore");

#else

    m_parent->updateMixerSwitches(allow_input,
				  allow_output,
				  m_info.profile());
    bool ok = true;

#endif

    if (gta04a3 && (m_info.domain() == "Phone")) {
        gsmVoiceStart();
    }

    return ok;
}

bool StateFileAudioState::leave()
{
    return true;
}

/* We need a subclass for Headset, so we can indicate when the headset
   is available. */
class HeadsetAudioState : public StateFileAudioState
{
    Q_OBJECT
public:
    HeadsetAudioState(QByteArray domain,
		      int priority,
		      NeoAudioPlugin *parent);

    virtual bool isAvailable() const;
    virtual bool enter(QAudio::AudioCapability capability);

private slots:
    void onHeadsetModified();

private:
    QValueSpaceItem *m_headset;
};

HeadsetAudioState::HeadsetAudioState(QByteArray domain,
				     int priority,
				     NeoAudioPlugin *parent) :
    StateFileAudioState(domain,
			"Headset",
			priority,
			parent)
{
    m_headset =
        new QValueSpaceItem("/Hardware/Accessories/PortableHandsfree/Present",
                            this);
    connect(m_headset, SIGNAL(contentsChanged()), SLOT(onHeadsetModified()));
}

void HeadsetAudioState::onHeadsetModified()
{
    bool avail = m_headset->value().toBool();

    qLog(AudioState) << __PRETTY_FUNCTION__ << avail;

    emit availabilityChanged(avail);
}

bool HeadsetAudioState::isAvailable() const
{
    return m_headset->value().toBool();
}

bool HeadsetAudioState::enter(QAudio::AudioCapability cap)
{
    bool ok = writeToFile("/sys/devices/virtual/gpio/gpio55/value", "1", 1) // switch off video out
      && StateFileAudioState::enter(cap);

    return ok;
}

#ifdef QTOPIA_BLUETOOTH
class BluetoothAudioState : public QAudioState
{
    Q_OBJECT
public:
    explicit BluetoothAudioState(bool isPhone,
				 int priority,
				 QObject * parent = 0);
    virtual ~ BluetoothAudioState();

    virtual QAudioStateInfo info() const;
    virtual QAudio::AudioCapabilities capabilities() const;

    virtual bool isAvailable() const;
    virtual bool enter(QAudio::AudioCapability capability);
    virtual bool leave();

private slots:
    void bluetoothAudioStateChanged();
    void headsetDisconnected();
    void headsetConnected();

private:
    bool resetCurrAudioGateway();

private:
    QList < QBluetoothAudioGateway * >m_audioGateways;
    bool m_isPhone;
    QBluetoothAudioGateway *m_currAudioGateway;
    QAudioStateInfo m_info;
    bool m_isActive;
    bool m_isAvail;
};

BluetoothAudioState::BluetoothAudioState(bool isPhone,
					 int priority,
					 QObject * parent):
QAudioState(parent),
m_isPhone(isPhone), m_currAudioGateway(0), m_isActive(false), m_isAvail(false)
{
    QBluetoothAudioGateway *hf =
        new QBluetoothAudioGateway("BluetoothHandsfree");
    m_audioGateways.append(hf);
    qLog(AudioState) << "Handsfree audio gateway: " << hf;

    QBluetoothAudioGateway *hs = new QBluetoothAudioGateway("BluetoothHeadset");
    m_audioGateways.append(hs);
    qLog(AudioState) << "Headset audio gateway: " << hs;

    for (int i = 0; i < m_audioGateways.size(); ++i) {
        QBluetoothAudioGateway *gateway = m_audioGateways.at(i);
        connect(gateway, SIGNAL(audioStateChanged()),
                SLOT(bluetoothAudioStateChanged()));
        connect(gateway, SIGNAL(headsetDisconnected()),
                SLOT(headsetDisconnected()));
        connect(gateway, SIGNAL(connectResult(bool, QString)),
                SLOT(headsetConnected()));
        connect(gateway, SIGNAL(newConnection(QBluetoothAddress)),
                SLOT(headsetConnected()));
    }

    if (isPhone) {
        m_info.setDomain("Phone");
        m_info.setProfile("PhoneBluetoothHeadset");
    } else {
        m_info.setDomain("Media");
        m_info.setProfile("MediaBluetoothHeadset");
    }
    m_info.setPriority(priority);

    m_info.setDisplayName(tr("Bluetooth Headset"));
    m_isAvail = resetCurrAudioGateway();
}

BluetoothAudioState::~BluetoothAudioState()
{
    qDeleteAll(m_audioGateways);
}

bool BluetoothAudioState::resetCurrAudioGateway()
{
    for (int i = 0; i < m_audioGateways.size(); ++i) {
        QBluetoothAudioGateway *gateway = m_audioGateways.at(i);
        if (gateway->isConnected()) {
            m_currAudioGateway = gateway;
            qLog(AudioState) << "Returning audiogateway to be:" <<
                m_currAudioGateway;
            return true;
        }
    }

    qLog(AudioState) << "No current audio gateway found";
    return false;
}

void BluetoothAudioState::bluetoothAudioStateChanged()
{
    qLog(AudioState) << "BluetoothAudioState::bluetoothAudioStateChanged()" <<
        m_isActive << m_currAudioGateway;

    if (m_isActive && (m_currAudioGateway || resetCurrAudioGateway())) {
        if (!m_currAudioGateway->audioEnabled()) {
            emit doNotUseHint();
        }
    } else if (!m_isActive && (m_currAudioGateway || resetCurrAudioGateway())) {
        if (m_currAudioGateway->audioEnabled()) {
            emit useHint();
        }
    }
}

void BluetoothAudioState::headsetConnected()
{
    if (!m_isAvail && resetCurrAudioGateway()) {
        m_isAvail = true;
        emit availabilityChanged(true);
    }
}

void BluetoothAudioState::headsetDisconnected()
{
    if (!resetCurrAudioGateway()) {
        m_isAvail = false;
        emit availabilityChanged(false);
    }
}

QAudioStateInfo BluetoothAudioState::info() const
{
    return m_info;
}

QAudio::AudioCapabilities BluetoothAudioState::capabilities()const
{

    if (m_isPhone) {
        return QAudio::OutputOnly;
    } else {
        return QAudio::OutputOnly | QAudio::InputOnly;
    }
}

bool BluetoothAudioState::isAvailable() const
{
    return m_isAvail;
}

bool BluetoothAudioState::enter(QAudio::AudioCapability capability)
{
    Q_UNUSED(capability)

        qLog(AudioState) << "BluetoothAudioState::enter()" << "isPhone" <<
        m_isPhone;

    if (m_currAudioGateway || resetCurrAudioGateway()) {
        m_currAudioGateway->connectAudio();
        //m_isActive = setAudioScenario(Scenario_GSMBluetooth);
    }

    return m_isActive;
}

bool BluetoothAudioState::leave()
{
    if (m_currAudioGateway || resetCurrAudioGateway()) {
        m_currAudioGateway->releaseAudio();
    }

    m_isActive = false;

    return true;
}
#endif

class NeoAudioPluginPrivate
{
public:
    QList < QAudioState * >m_states;
    QAudioState *null_state;
    QAudioMixer *mixer;
    QAudioElement *headset_mic_switch;
    QAudioElement *main_mic_switch;
    QAudioElement *playback_switch;
    QAudioElement *speaker_switch;
    QAudioElement *headset_left_switch;
    QAudioElement *headset_right_switch;
    QAudioElement *earpiece_switch;
};

NeoAudioPlugin::NeoAudioPlugin(QObject * parent):
QAudioStatePlugin(parent)
{
    m_data = new NeoAudioPluginPrivate;

#ifdef USE_STATE_FILES
    usePulse = QFile::exists("/usr/bin/pasuspender");
#endif

    // On A4+ models use HW sound routing, on A3 do SW routing
    gta04a3 = !(QFile::exists("/sys/class/gpio/gpio186/value"));

    /* Priority ordering for phone calls: Headset (if available),
       Bluetooth (if available), Earpiece, Speaker. */
    m_data->m_states.
      push_back(new HeadsetAudioState("Phone", 1, this));
#ifdef QTOPIA_BLUETOOTH
    m_data->m_states.
      push_back(new BluetoothAudioState(true, 2, this));
#endif
    m_data->m_states.
      push_back(new StateFileAudioState("Phone", "Earpiece", 3, this));
    m_data->m_states.
      push_back(new StateFileAudioState("Phone", "Speaker", 4, this));

    /* Priority ordering for media playback: Headset (if available),
       Bluetooth (if available), Speaker, Earpiece. */
    m_data->m_states.
      push_back(new HeadsetAudioState("Media", 1, this));
#ifdef QTOPIA_BLUETOOTH
    m_data->m_states.
      push_back(new BluetoothAudioState(false, 2, this));
#endif
    m_data->m_states.
      push_back(new StateFileAudioState("Media", "Speaker", 3, this));
    m_data->m_states.
      push_back(new StateFileAudioState("Media", "Earpiece", 4, this));

    /* Recording when in the Media domain. */
    m_data->m_states.
      push_back(new StateFileAudioState("Media", "Recording", 5, this));

    /* Priority ordering for ringtone is the same as for media, but
       BluetoothAudioState doesn't yet support the Ringtone domain. */
    m_data->m_states.
      push_back(new HeadsetAudioState("Ringtone", 1, this));
    m_data->m_states.
      push_back(new StateFileAudioState("Ringtone", "Speaker", 3, this));
    m_data->m_states.
      push_back(new StateFileAudioState("Ringtone", "Earpiece", 4, this));

    /* We shouldn't have to do anything here to set up the initial
       state.  Code elsewhere should know that we initially want the
       Media domain and choose the best available audio state for
       that. */

    /* Also create a Null state that allows no audio input or output.
       It may be useful for power saving to enter this before
       suspending. */
    m_data->null_state = new StateFileAudioState("Null", "Null", 6, this);

    /* Find the mixer switches that we will need. */
    m_data->mixer = new QAudioMixer();
    QList<QAudioElement*> e = m_data->mixer->elements();
    m_data->headset_mic_switch = NULL;
    m_data->main_mic_switch = NULL;
    m_data->playback_switch = NULL;
    m_data->speaker_switch = NULL;
    m_data->headset_left_switch = NULL;
    m_data->headset_right_switch = NULL;
    m_data->earpiece_switch = NULL;
    for (int i = 0; i < e.size(); i++) {
	QAudioElement *elem = e.at(i);
	if (elem->getName() == "Analog Left Headset Mic") {
	    m_data->headset_mic_switch = elem;
	}
	else if (elem->getName() == "Analog Left Main Mic") {
	    m_data->main_mic_switch = elem;
	}
	else if (elem->getName() == "DAC2 Analog") {
	    m_data->playback_switch = elem;
	}
	else if (elem->getName() == "HandsfreeL") {
	    m_data->speaker_switch = elem;
	}
	else if (elem->getName() == "HeadsetL Mixer AudioL2") {
	    m_data->headset_left_switch = elem;
	}
	else if (elem->getName() == "HeadsetR Mixer AudioR2") {
	    m_data->headset_right_switch = elem;
	}
	else if (elem->getName() == "Earpiece Mixer AudioL2") {
	    m_data->earpiece_switch = elem;
	}
    }
}

NeoAudioPlugin::~NeoAudioPlugin()
{
    delete(m_data->mixer);
    delete(m_data->null_state);
    qDeleteAll(m_data->m_states);

    delete m_data;
}

QList < QAudioState * >NeoAudioPlugin::statesProvided()const
{
    return m_data->m_states;
}

void NeoAudioPlugin::updateMixerSwitches(bool allow_input,
					 bool allow_output,
					 QString profile)
{
    qLog(AudioState)
	<< "updateMixerSwitches" << allow_input << allow_output << profile;

    m_data->headset_mic_switch->setMute(!(allow_input && (profile == "Headset")));
    m_data->main_mic_switch->setMute(!(allow_input && (profile != "Headset")));
    m_data->playback_switch->setMute(!(allow_output));
    m_data->speaker_switch->setMute(!(allow_output && (profile == "Speaker")));
    m_data->headset_left_switch->setMute(!(allow_output && (profile == "Headset")));
    m_data->headset_right_switch->setMute(!(allow_output && (profile == "Headset")));
    m_data->earpiece_switch->setMute(!(allow_output && (profile == "Earpiece")));
}

Q_EXPORT_PLUGIN2(neoaudio_plugin, NeoAudioPlugin)
#include "neoaudioplugin.moc"
