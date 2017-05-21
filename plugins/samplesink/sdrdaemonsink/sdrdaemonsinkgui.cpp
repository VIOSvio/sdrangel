///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <QDebug>

#include <QTime>
#include <QDateTime>
#include <QString>
#include <QFileDialog>
#include <QMessageBox>

#include "ui_sdrdaemonsinkgui.h"
#include "plugin/pluginapi.h"
#include "gui/colormapper.h"
#include "gui/glspectrum.h"
#include "dsp/dspengine.h"
#include "dsp/dspcommands.h"

#include "mainwindow.h"

#include "device/devicesinkapi.h"
#include "sdrdaemonsinkgui.h"

SDRdaemonSinkGui::SDRdaemonSinkGui(DeviceSinkAPI *deviceAPI, QWidget* parent) :
	QWidget(parent),
	ui(new Ui::SDRdaemonSinkGui),
	m_deviceAPI(deviceAPI),
	m_settings(),
	m_deviceSampleSink(0),
	m_sampleRate(0),
	m_samplesCount(0),
	m_tickCount(0),
	m_lastEngineState((DSPDeviceSinkEngine::State)-1)
{
	ui->setupUi(this);

	ui->centerFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
	ui->centerFrequency->setValueRange(7, 0, pow(10,7));

    ui->sampleRate->setColorMapper(ColorMapper(ColorMapper::GrayGreenYellow));
    ui->sampleRate->setValueRange(7, 32000U, 9000000U);

	connect(&(m_deviceAPI->getMainWindow()->getMasterTimer()), SIGNAL(timeout()), this, SLOT(tick()));
	connect(&m_updateTimer, SIGNAL(timeout()), this, SLOT(updateHardware()));
	connect(&m_statusTimer, SIGNAL(timeout()), this, SLOT(updateStatus()));
	m_statusTimer.start(500);

	displaySettings();

	m_deviceSampleSink = new SDRdaemonSinkOutput(m_deviceAPI, m_deviceAPI->getMainWindow()->getMasterTimer());
    connect(m_deviceSampleSink->getOutputMessageQueueToGUI(), SIGNAL(messageEnqueued()), this, SLOT(handleSinkMessages()));
	m_deviceAPI->setSink(m_deviceSampleSink);

    connect(m_deviceAPI->getDeviceOutputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleDSPMessages()), Qt::QueuedConnection);
}

SDRdaemonSinkGui::~SDRdaemonSinkGui()
{
	delete ui;
}

void SDRdaemonSinkGui::destroy()
{
	delete this;
}

void SDRdaemonSinkGui::setName(const QString& name)
{
	setObjectName(name);
}

QString SDRdaemonSinkGui::getName() const
{
	return objectName();
}

void SDRdaemonSinkGui::resetToDefaults()
{
	m_settings.resetToDefaults();
	displaySettings();
	sendSettings();
}

qint64 SDRdaemonSinkGui::getCenterFrequency() const
{
	return m_settings.m_centerFrequency;
}

void SDRdaemonSinkGui::setCenterFrequency(qint64 centerFrequency)
{
    m_settings.m_centerFrequency = centerFrequency;
	displaySettings();
	sendSettings();
}

QByteArray SDRdaemonSinkGui::serialize() const
{
	return m_settings.serialize();
}

bool SDRdaemonSinkGui::deserialize(const QByteArray& data)
{
	if(m_settings.deserialize(data)) {
		displaySettings();
		sendSettings();
		return true;
	} else {
		resetToDefaults();
		return false;
	}
}

bool SDRdaemonSinkGui::handleMessage(const Message& message)
{
	if (SDRdaemonSinkOutput::MsgReportSDRdaemonSinkStreamTiming::match(message))
	{
		m_samplesCount = ((SDRdaemonSinkOutput::MsgReportSDRdaemonSinkStreamTiming&)message).getSamplesCount();
		updateWithStreamTime();
		return true;
	}
	else
	{
		return false;
	}
}

void SDRdaemonSinkGui::handleDSPMessages()
{
    Message* message;

    while ((message = m_deviceAPI->getDeviceOutputMessageQueue()->pop()) != 0)
    {
        qDebug("FileSinkGui::handleDSPMessages: message: %s", message->getIdentifier());

        if (DSPSignalNotification::match(*message))
        {
            DSPSignalNotification* notif = (DSPSignalNotification*) message;
            qDebug("FileSinkGui::handleDSPMessages: SampleRate:%d, CenterFrequency:%llu", notif->getSampleRate(), notif->getCenterFrequency());
            m_sampleRate = notif->getSampleRate();
            m_deviceCenterFrequency = notif->getCenterFrequency();
            updateSampleRateAndFrequency();

            delete message;
        }
    }
}

void SDRdaemonSinkGui::handleSinkMessages()
{
    Message* message;

    while ((message = m_deviceSampleSink->getOutputMessageQueueToGUI()->pop()) != 0)
    {
        //qDebug("FileSourceGui::handleSourceMessages: message: %s", message->getIdentifier());

        if (handleMessage(*message))
        {
            delete message;
        }
    }
}

void SDRdaemonSinkGui::updateSampleRateAndFrequency()
{
    m_deviceAPI->getSpectrum()->setSampleRate(m_sampleRate);
    m_deviceAPI->getSpectrum()->setCenterFrequency(m_deviceCenterFrequency);
    ui->deviceRateText->setText(tr("%1k").arg((float)(m_sampleRate*(1<<m_settings.m_log2Interp)) / 1000));
}

void SDRdaemonSinkGui::displaySettings()
{
    ui->centerFrequency->setValue(m_settings.m_centerFrequency / 1000);
    ui->sampleRate->setValue(m_settings.m_sampleRate);
}

void SDRdaemonSinkGui::sendSettings()
{
    if(!m_updateTimer.isActive())
        m_updateTimer.start(100);
}


void SDRdaemonSinkGui::updateHardware()
{
    qDebug() << "FileSinkGui::updateHardware";
    SDRdaemonSinkOutput::MsgConfigureSDRdaemonSink* message = SDRdaemonSinkOutput::MsgConfigureSDRdaemonSink::create(m_settings);
    m_deviceSampleSink->getInputMessageQueue()->push(message);
    m_updateTimer.stop();
}

void SDRdaemonSinkGui::updateStatus()
{
    int state = m_deviceAPI->state();

    if(m_lastEngineState != state)
    {
        switch(state)
        {
            case DSPDeviceSinkEngine::StNotStarted:
                ui->startStop->setStyleSheet("QToolButton { background:rgb(79,79,79); }");
                break;
            case DSPDeviceSinkEngine::StIdle:
                ui->startStop->setStyleSheet("QToolButton { background-color : blue; }");
                break;
            case DSPDeviceSinkEngine::StRunning:
                ui->startStop->setStyleSheet("QToolButton { background-color : red; }");
                break;
            case DSPDeviceSinkEngine::StError:
                ui->startStop->setStyleSheet("QToolButton { background-color : magenta; }");
                QMessageBox::information(this, tr("Message"), m_deviceAPI->errorMessage());
                break;
            default:
                break;
        }

        m_lastEngineState = state;
    }
}

void SDRdaemonSinkGui::on_centerFrequency_changed(quint64 value)
{
    m_settings.m_centerFrequency = value * 1000;
    sendSettings();
}

void SDRdaemonSinkGui::on_sampleRate_changed(quint64 value)
{
    m_settings.m_sampleRate = value;
    sendSettings();
}

void SDRdaemonSinkGui::on_interp_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }

    m_settings.m_log2Interp = index;
    updateSampleRateAndFrequency();
    sendSettings();
}

void SDRdaemonSinkGui::on_startStop_toggled(bool checked)
{
    if (checked)
    {
        if (m_deviceAPI->initGeneration())
        {
            if (!m_deviceAPI->startGeneration())
            {
                qDebug("FileSinkGui::on_startStop_toggled: device start failed");
            }

            DSPEngine::instance()->startAudioInput();
        }
    }
    else
    {
        m_deviceAPI->stopGeneration();
        DSPEngine::instance()->stopAudioInput();
    }
}

void SDRdaemonSinkGui::updateWithStreamTime()
{
	int t_sec = 0;
	int t_msec = 0;

	if (m_settings.m_sampleRate > 0){
		t_msec = ((m_samplesCount * 1000) / m_settings.m_sampleRate) % 1000;
		t_sec = m_samplesCount / m_settings.m_sampleRate;
	}

	QTime t(0, 0, 0, 0);
	t = t.addSecs(t_sec);
	t = t.addMSecs(t_msec);
	QString s_timems = t.toString("hh:mm:ss.zzz");
	//ui->relTimeText->setText(s_timems); TODO with absolute time
}

void SDRdaemonSinkGui::tick()
{
	if ((++m_tickCount & 0xf) == 0)
	{
		SDRdaemonSinkOutput::MsgConfigureSDRdaemonSinkStreamTiming* message = SDRdaemonSinkOutput::MsgConfigureSDRdaemonSinkStreamTiming::create();
		m_deviceSampleSink->getInputMessageQueue()->push(message);
	}
}
