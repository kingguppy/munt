/* Copyright (C) 2011 Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "audiodrv/AudioDriver.h"
#include "SynthWidget.h"
#include "ui_SynthWidget.h"
#include "Master.h"

SynthWidget::SynthWidget(Master *master, SynthRoute *useSynthRoute, QWidget *parent) :
	QWidget(parent),
	synthRoute(useSynthRoute),
	ui(new Ui::SynthWidget) {
	ui->setupUi(this);
	QListIterator<AudioDevice *> audioDeviceIt(master->getAudioDevices());
	while(audioDeviceIt.hasNext()) {
		AudioDevice *audioDevice = audioDeviceIt.next();
		handleAudioDeviceAdded(audioDevice);
	}
	connect(synthRoute, SIGNAL(stateChanged(SynthRouteState)), SLOT(handleSynthRouteState(SynthRouteState)));
	connect(ui->audioDeviceComboBox, SIGNAL(currentIndexChanged(int)), SLOT(handleAudioDeviceIndex(int)));
	synthRoute->connect(this, SIGNAL(audioDeviceChanged(AudioDevice*)), SLOT(setAudioDevice(AudioDevice*)));
	connect(master, SIGNAL(audioDeviceAdded(AudioDevice*)), SLOT(handleAudioDeviceAdded(AudioDevice*)));
	connect(master, SIGNAL(audioDeviceRemoved(AudioDevice*)), SLOT(handleAudioDeviceRemoved(AudioDevice*)));
	handleSynthRouteState(synthRoute->getState());
}

SynthWidget::~SynthWidget() {
	delete ui;
}

SynthRoute *SynthWidget::getSynthRoute() {
	return synthRoute;
}

void SynthWidget::handleAudioDeviceAdded(AudioDevice *device) {
	ui->audioDeviceComboBox->addItem(device->driver->name + ": " + device->name, qVariantFromValue(device));
}

void SynthWidget::handleAudioDeviceRemoved(AudioDevice *device) {
	ui->audioDeviceComboBox->removeItem(ui->audioDeviceComboBox->findData(qVariantFromValue(device)));
}

void SynthWidget::handleAudioDeviceIndex(int audioDeviceIndex) {
	//AudioDevice *currentAudioDevice = ui->audioDeviceComboBox->itemData(ui->audioDeviceComboBox->currentIndex());
	//ui->audioDeviceComboBox->itemData()
}

void SynthWidget::handleSynthRouteState(SynthRouteState SynthRouteState) {
	switch(SynthRouteState) {
	case SynthRouteState_OPEN:
		ui->startButton->setEnabled(false);
		ui->stopButton->setEnabled(true);
		ui->audioDeviceComboBox->setEnabled(false);
		ui->statusLabel->setText("Open");
		break;
	case SynthRouteState_OPENING:
		ui->startButton->setEnabled(false);
		ui->stopButton->setEnabled(false);
		ui->audioDeviceComboBox->setEnabled(false);
		ui->statusLabel->setText("Opening");
		break;
	case SynthRouteState_CLOSING:
		ui->startButton->setEnabled(false);
		ui->stopButton->setEnabled(false);
		ui->audioDeviceComboBox->setEnabled(false);
		ui->statusLabel->setText("Closing");
		break;
	case SynthRouteState_CLOSED:
		ui->startButton->setEnabled(true);
		ui->stopButton->setEnabled(false);
		ui->audioDeviceComboBox->setEnabled(true);
		ui->statusLabel->setText("Closed");
		break;
	}
}

void SynthWidget::on_synthPropertiesButton_clicked()
{
	spd.exec();
}

void SynthWidget::on_startButton_clicked()
{
	if (!synthRoute->open()) {
		ui->statusLabel->setText("Open failed :(");
	}
}

void SynthWidget::on_stopButton_clicked()
{
	synthRoute->close();
	ui->statusLabel->setText("Closed");
}
