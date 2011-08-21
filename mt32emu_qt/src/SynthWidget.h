#ifndef SYNTHWIDGET_H
#define SYNTHWIDGET_H

#include <QWidget>

#include "SynthRoute.h"
#include "SynthPropertiesDialog.h"

class Master;
class AudioDevice;

namespace Ui {
	class SynthWidget;
}

class SynthWidget : public QWidget
{
	Q_OBJECT

public:
	explicit SynthWidget(Master *master, SynthRoute *synthRoute, QWidget *parent = 0);
	~SynthWidget();
	SynthRoute *getSynthRoute();

private:
	SynthRoute *synthRoute;
	Ui::SynthWidget *ui;
	SynthPropertiesDialog spd;

private slots:
	void on_startButton_clicked();
	void on_synthPropertiesButton_clicked();
	void on_stopButton_clicked();
	void handleSynthRouteState(SynthRouteState state);
	void handleAudioDeviceIndex(int audioDeviceIndex);
	void handleAudioDeviceAdded(AudioDevice *audioDevice);
	void handleAudioDeviceRemoved(AudioDevice *audioDevice);
};

#endif // SYNTHWIDGET_H
