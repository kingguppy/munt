#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#include <QObject>
#include <QtGlobal>
#include <QList>
#include <QString>
#include <QMetaType>

class AudioDriver;
class QSynth;

class AudioStream {
public:
	//virtual void suspend() = 0;
	//virtual void unsuspend() = 0;
	virtual ~AudioStream() {};
};

class AudioDevice {
public:
	const AudioDriver *driver;
	const QString name;
	AudioDevice(AudioDriver *driver, QString name);
	virtual ~AudioDevice() {};
	virtual AudioStream *startAudioStream(QSynth *synth, unsigned int sampleRate) = 0;
};

Q_DECLARE_METATYPE(AudioDevice*);

class AudioDriver {
public:
	const QString name;
	AudioDriver(QString useName) : name(useName) {};
	virtual ~AudioDriver() {};
};

#endif
