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

bool usePulse;
bool gta04a3;

static bool writeToFile(const char *filename, const char *val, int len)
{
    qLog(AudioState) << "echo " << val << " > " << filename;
    return (bool) Qtopia::writeFile(filename, val, len);
}

QProcess *voicePs = NULL;

static bool gsmVoiceStop()
{
    // Move back alsa config (used e.g. by blueooth a2dp sound)
    if(QFile::exists("/home/root/.asoundrc.tmp")) {
        QFile::rename("/home/root/.asoundrc.tmp", "/home/root/.asoundrc");
    }

    if (voicePs == NULL) {
        return true;
    }
    qLog(AudioState) << "terminating gsm-voice-routing pid " << voicePs->pid();
    voicePs->terminate();
    if (!voicePs->waitForFinished(1000)) {
        qWarning() << "gsm-voice-routing process failed to terminate";
        voicePs->kill();
    }
    delete(voicePs);
    voicePs = NULL;
    return true;
}

static bool gsmVoiceStart()
{
    if (voicePs != NULL) {
        return true;
    }

    // Move away alsa config (used e.g. by blueooth a2dp sound)
    if(QFile::exists("/home/root/.asoundrc")) {
        QFile::rename("/home/root/.asoundrc", "/home/root/.asoundrc.tmp");
    }
    
    voicePs = new QProcess();
    QStringList args;

    // Dump output always to stderr if audio logging is enabled
    if (qLogEnabled(AudioState)) {
        QStringList env = QProcess::systemEnvironment();
        for (int i = 0; i < env.count(); i++) {
            if (env.at(i).startsWith("GSM_VOICE_ROUTING_LOGFILE")) {
                env.removeAt(i);
                voicePs->setEnvironment(env);
                break;
            }
        }
        voicePs->setProcessChannelMode(QProcess::ForwardedChannels);
    }

    if(usePulse) {
        args.insert(0, "gsm-voice-routing");
        args.insert(0, "--");
        qLog(AudioState) << "pasuspender " << args;
        voicePs->start("pasuspender", args);
    } else
        voicePs->start("gsm-voice-routing");

    if (voicePs->waitForStarted(3000)) {
        qLog(AudioState) << "starting gsm-voice-routing pid " << voicePs->pid();
        return true;
    }
    qWarning() << "failed to start gsm-voice-routing: " <<
        voicePs->errorString();
    return false;
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

    m_parent->updateMixerSwitches(allow_input,
				  allow_output,
				  m_info.profile(),
				  m_info.domain() == "Phone");

    if (gta04a3 && (m_info.domain() == "Phone")) {
        gsmVoiceStart();
    }

    return true;
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
    QAudioMixer *mixer;
    QAudioElement *headset_mic_switch;
    QAudioElement *main_mic_switch;
    QAudioElement *playback_switch;
    QAudioElement *speaker_switch;
    QAudioElement *headset_left_switch;
    QAudioElement *headset_right_switch;
    QAudioElement *earpiece_switch;
    QAudioElement *voice_route;
};

NeoAudioPlugin::NeoAudioPlugin(QObject * parent):
QAudioStatePlugin(parent)
{
    m_data = new NeoAudioPluginPrivate;

    usePulse = QFile::exists("/usr/bin/pasuspender");

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
    m_data->voice_route = NULL;
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
	else if (elem->getName() == "Voice route") {
	    m_data->voice_route = elem;
	}
    }

    /* We don't have to do anything here to install the initial state.
       Code elsewhere works out that we initially want the Media
       domain and selects the best available audio state for that
       domain, and calls (indirectly) the enter() method for that
       state. */
}

NeoAudioPlugin::~NeoAudioPlugin()
{
    delete(m_data->mixer);
    qDeleteAll(m_data->m_states);

    delete m_data;
}

QList < QAudioState * >NeoAudioPlugin::statesProvided()const
{
    return m_data->m_states;
}

void NeoAudioPlugin::updateMixerSwitches(bool allow_input,
					 bool allow_output,
					 QString profile,
					 bool phone)
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

    if (!gta04a3) {
	qLog(AudioState) << "phone" << phone;

	// It saves power, in general, but especially during suspend,
	// for the "Voice route" control to be set to "Voice to SoC".
	// Hence on A4 (and later) we only set this control to "Voice
	// to twl4030" (which is the HW routing setting) when in a
	// phone call, and set it back to "Voice to Soc" in all other
	// circumstances.
	m_data->voice_route->setOption(phone
				       ? "Voice to twl4030"
				       : "Voice to SoC");
    }
}

Q_EXPORT_PLUGIN2(neoaudio_plugin, NeoAudioPlugin)
#include "neoaudioplugin.moc"
