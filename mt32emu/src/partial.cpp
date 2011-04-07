/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "mt32emu.h"
#include "mmath.h"

using namespace MT32Emu;

Partial::Partial(Synth *useSynth, int useDebugPartialNum) :
	synth(useSynth), debugPartialNum(useDebugPartialNum), tva(new TVA(this)), tvp(new TVP(this)), tvf(new TVF(this)) {
	ownerPart = -1;
	poly = NULL;
	pair = NULL;
}

Partial::~Partial() {
	delete tva;
	delete tvp;
	delete tvf;
}

int Partial::getOwnerPart() const {
	return ownerPart;
}

bool Partial::isActive() const {
	return ownerPart > -1;
}

const Poly *Partial::getPoly() const {
	return poly;
}

void Partial::activate(int part) {
	// This just marks the partial as being assigned to a part
	ownerPart = part;
}

void Partial::deactivate() {
	if (!isActive()) {
		return;
	}
	ownerPart = -1;
	if (poly != NULL) {
		poly->partialDeactivated(this);
		if (pair != NULL) {
			pair->pair = NULL;
		}
	}
}

// DEPRECATED: This should probably go away eventually, it's currently only used as a kludge to protect our old assumptions that
// rhythm part notes were always played as key MIDDLEC.
int Partial::getKey() const {
	if (poly == NULL) {
		return -1;
	} else if (ownerPart == 8) {
		// FIXME: Hack, should go away after new pitch stuff is committed (and possibly some TVF changes)
		return MIDDLEC;
	} else {
		return poly->getKey();
	}
}

void Partial::startPartial(const Part *part, Poly *usePoly, const PatchCache *usePatchCache, const MemParams::RhythmTemp *rhythmTemp, Partial *pairPartial) {
	if (usePoly == NULL || usePatchCache == NULL) {
		synth->printDebug("*** Error: Starting partial for owner %d, usePoly=%s, usePatchCache=%s", ownerPart, usePoly == NULL ? "*** NULL ***" : "OK", usePatchCache == NULL ? "*** NULL ***" : "OK");
		return;
	}
	patchCache = usePatchCache;
	poly = usePoly;
	mixType = patchCache->structureMix;
	structurePosition = patchCache->structurePosition;

	play = true;

	Bit8u panSetting = rhythmTemp != NULL ? rhythmTemp->panpot : part->getPatchTemp()->panpot;
	if (mixType == 3) {
		if (structurePosition == 0) {
			if (panSetting > 7) {
				panSetting = (panSetting - 7) * 2;
			} else {
				panSetting = 0;
			}
		} else {
			if (panSetting < 7) {
				panSetting = panSetting * 2;
			} else {
				panSetting = 14;
			}
		}
		// Do a normal mix independent of any pair partial.
		mixType = 0;
		pairPartial = NULL;
	}
	// FIXME: Sample analysis suggests that this is linear, but there are some some quirks that still need to be resolved.
	// On the real devices, there are only 8 real pan positions.
	stereoVolume.leftVol = panSetting / 14.0f;
	stereoVolume.rightVol = 1.0f - stereoVolume.leftVol;

	if (patchCache->PCMPartial) {
		pcmNum = patchCache->pcm;
		if (synth->controlROMMap->pcmCount > 128) {
			// CM-32L, etc. support two "banks" of PCMs, selectable by waveform type parameter.
			if (patchCache->waveform > 1) {
				pcmNum += 128;
			}
		}
		pcmWave = &synth->pcmWaves[pcmNum];
	} else {
		pcmWave = NULL;
		wavePos = 0.0f;
	}

	// CONFIRMED: pulseWidthVal calculation is based on information from Mok
	pulseWidthVal = (poly->getVelocity() - 64) * (patchCache->srcPartial.wg.pulseWidthVeloSensitivity - 7) + synth->tables.pulseWidth100To255[patchCache->srcPartial.wg.pulseWidth];
	if (pulseWidthVal < 0) {
		pulseWidthVal = 0;
	} else if (pulseWidthVal > 255) {
		pulseWidthVal = 255;
	}

	pcmPosition = 0.0f;
	intPCMPosition = 0;
	pair = pairPartial;
	alreadyOutputed = false;
	tva->reset(part, patchCache->partialParam, rhythmTemp);
	tvp->reset(part, patchCache->partialParam);
	tvf->reset(patchCache->partialParam, tvp->getBasePitch());
	memset(history, 0, sizeof(history));
}

float Partial::getPCMSample(unsigned int position) {
	if (position >= pcmWave->len) {
		if (!pcmWave->loop) {
			return 0;
		}
		position = position % pcmWave->len;
	}
	return synth->pcmROMData[pcmWave->addr + position];
}

unsigned long Partial::generateSamples(float *partialBuf, unsigned long length) {
	if (!isActive() || alreadyOutputed) {
		return 0;
	}
	if (poly == NULL) {
		synth->printDebug("*** ERROR: poly is NULL at Partial::generateSamples()!");
		return 0;
	}

	alreadyOutputed = true;

	// Generate samples

	unsigned long sampleNum;
	for (sampleNum = 0; sampleNum < length; sampleNum++) {
		float sample = 0;
		float amp = tva->nextAmp();
		if (!tva->isPlaying()) {
			deactivate();
			break;
		}

		Bit16u pitch = tvp->nextPitch();

		float freq = synth->tables.pitchToFreq[pitch];

		if (patchCache->PCMPartial) {
			// Render PCM waveform
			int len = pcmWave->len;
			if (intPCMPosition >= len && !pcmWave->loop) {
				// We're now past the end of a non-looping PCM waveform so it's time to die.
				play = false;
				deactivate();
				break;
			}
			Bit32u pcmAddr = pcmWave->addr;
			float positionDelta = freq * 2048.0f / synth->myProp.sampleRate;
			float newPCMPosition = pcmPosition + positionDelta;
			int newIntPCMPosition = (int)newPCMPosition;

			// Linear interpolation
			float firstSample = synth->pcmROMData[pcmAddr + intPCMPosition];
			float nextSample = getPCMSample(intPCMPosition + 1);
			sample = firstSample + (nextSample - firstSample) * (pcmPosition - intPCMPosition);
			if (pcmWave->loop) {
				newPCMPosition = fmod(newPCMPosition, (float)pcmWave->len);
				newIntPCMPosition = newIntPCMPosition % pcmWave->len;
			}
			pcmPosition = newPCMPosition;
			intPCMPosition = newIntPCMPosition;
		} else {
			// Render synthesised waveform
			float resAmp = EXP2F(-9.0f *(1.0f - patchCache->srcPartial.tvf.resonance / 30.0f));

			float cutoffVal = tvf->getBaseCutoff();
			// The modifier may not be supposed to be added to the cutoff at all -
			// it may for example need to be multiplied in some way.
			cutoffVal += tvf->nextCutoffModifier();

			// Wave lenght in samples
			float waveLen = synth->myProp.sampleRate / freq;

			// Anti-aliasing feature
			if (waveLen < 4.0f) {
				waveLen = 4.0f;
			}

			// Init cosineLen
			float cosineLen = 0.5f * waveLen;
			if (cutoffVal > 128) {
				float ft = (cutoffVal - 128) / 128.0f;
				cosineLen *= EXP2F(-8.0f * ft); // found from sample analysis
			}

			// Anti-aliasing feature
			if (cosineLen < 2.0f) {
				cosineLen = 2.0f;
				resAmp = 0.0f;
			}

			// Start playing in center of first cosine segment
			// relWavePos is shifted by a half of cosineLen
			float relWavePos = wavePos + 0.5f * cosineLen;
			if (relWavePos > waveLen) {
				relWavePos -= waveLen;
			}

			// Ratio of negative segment to waveLen
			float pulseLen = 0.5f;
			if (pulseWidthVal > 128) {
				// Formula determined from sample analysis.
				float pt = 0.5f / 127.0f * (pulseWidthVal - 128);
				pulseLen += (1.239f - pt) * pt;
			}
			pulseLen *= waveLen;

			float lLen = pulseLen - cosineLen;

			// Ignore pulsewidths too high for given freq
			if (lLen < 0.0f) {
				lLen = 0.0f;
			}

			// Ignore pulsewidths too high for given freq and cutoff
			float hLen = waveLen - lLen - 2 * cosineLen;
			if (hLen < 0.0f) {
				hLen = 0.0f;
			}

			// Correct resAmp for cutoff in range 50..60
			if (cutoffVal < 138) {
				resAmp *= (1.0f - (138 - cutoffVal) / 10.0f);
			}

			// Produce filtered square wave with 2 cosine waves on slopes

			// 1st cosine segment
			if (relWavePos < cosineLen) {
				sample = -cosf(FLOAT_PI * relWavePos / cosineLen);
			} else

			// high linear segment
			if (relWavePos < (cosineLen + hLen)) {
				sample = 1.f;
			} else

			// 2nd cosine segment
			if (relWavePos < (2 * cosineLen + hLen)) {
				sample = cosf(FLOAT_PI * (relWavePos - (cosineLen + hLen)) / cosineLen);
			} else {

			// low linear segment
				sample = -1.f;
			}

			if (cutoffVal < 128) {

				// Attenuate samples below cutoff 50 another way
				// Found by sample analysis
				sample *= EXP2F(-0.125f * (128 - cutoffVal));
			} else {

				// Add resonance sine. Effective for cutoff > 50 only
				float resSample = 1.0f;
				float resAmpFade = 0.0f;

				// Now relWavePos counts from the middle of first cosine
				relWavePos = wavePos;

				// negative segments
				if (!(relWavePos < (cosineLen + hLen))) {
					resSample = -resSample;
					relWavePos -= cosineLen + hLen;
				}

				// Resonance sine WG
				resSample *= sinf(FLOAT_PI * relWavePos / cosineLen);

				// Resonance sine amp
				resAmpFade = RESAMPMAX - RESAMPFADE * (relWavePos / cosineLen);

				// Now relWavePos set negative to the left from center of any cosine
				relWavePos = wavePos;

				// negative segment
				if (!(wavePos < (waveLen - 0.5f * cosineLen))) {
					relWavePos -= waveLen;
				} else

				// positive segment
				if (!(wavePos < (hLen + 0.5f * cosineLen))) {
					relWavePos -= cosineLen + hLen;
				}

				// Fading to zero while in first half of cosine segment to avoid jumps in the wave
				// FIXME: sample analysis suggests that this window isn't linear
				if (relWavePos < 0.0f) {
//					resAmpFade *= -relWavePos / (0.5f * cosineLen);                                  // linear
					resAmpFade *= 0.5f * (1.0f - cosf(FLOAT_PI * relWavePos / (0.5f * cosineLen)));  // full cosine
//					resAmpFade *= (1.0f - cosf(0.5f * FLOAT_PI * relWavePos / (0.5f * cosineLen)));  // half cosine
				}

				sample += resSample * resAmp * resAmpFade;
			}

			// sawtooth waves
			if ((patchCache->waveform & 1) != 0) {
				sample *= cosf(FLOAT_2PI * wavePos / waveLen);
			}

			wavePos++;
			if (wavePos > waveLen)
				wavePos -= waveLen;
		}

		// Multiply sample with current TVA value
		sample *= amp;
		*partialBuf++ = sample;
	}
	// At this point, sampleNum represents the number of samples rendered
	return sampleNum;
}

float *Partial::mixBuffersRingMix(float *buf1, float *buf2, unsigned long len) {
	if (buf1 == NULL) {
		return NULL;
	}
	if (buf2 == NULL) {
		return buf1;
	}

	while (len--) {
		// FIXME: At this point we have no idea whether this is remotely correct...
		*buf1 = *buf1 * *buf2 + *buf1;
		buf1++;
		buf2++;
	}
	return buf1;
}

float *Partial::mixBuffersRing(float *buf1, float *buf2, unsigned long len) {
	if (buf1 == NULL) {
		return NULL;
	}
	if (buf2 == NULL) {
		return NULL;
	}

	while (len--) {
		// FIXME: At this point we have no idea whether this is remotely correct...
		*buf1 = *buf1 * *buf2;
		buf1++;
		buf2++;
	}
	return buf1;
}

bool Partial::hasRingModulatingSlave() const {
	return pair != NULL && structurePosition == 0 && (mixType == 1 || mixType == 2);
}

bool Partial::isRingModulatingSlave() const {
	return pair != NULL && structurePosition == 1 && (mixType == 1 || mixType == 2);
}

bool Partial::isPCM() const {
	return pcmWave != NULL;
}

const ControlROMPCMStruct *Partial::getControlROMPCMStruct() const {
	if (pcmWave != NULL) {
		return pcmWave->controlROMPCMStruct;
	}
	return NULL;
}

Synth *Partial::getSynth() const {
	return synth;
}

bool Partial::produceOutput(float *leftBuf, float *rightBuf, unsigned long length) {
	if (!isActive() || alreadyOutputed || isRingModulatingSlave()) {
		return false;
	}
	if (poly == NULL) {
		synth->printDebug("*** ERROR: poly is NULL at Partial::produceOutput()!");
		return false;
	}

	float *partialBuf = &myBuffer[0];
	unsigned long numGenerated = generateSamples(partialBuf, length);
	if (mixType == 1 || mixType == 2) {
		float *pairBuf;
		unsigned long pairNumGenerated;
		if (pair == NULL) {
			pairBuf = NULL;
			pairNumGenerated = 0;
		} else {
			pairBuf = &pair->myBuffer[0];
			pairNumGenerated = pair->generateSamples(pairBuf, numGenerated);
			// pair will have been set to NULL if it deactivated within generateSamples()
			if (pair != NULL) {
				if (!isActive()) {
					pair->deactivate();
					pair = NULL;
				} else if (!pair->isActive()) {
					pair = NULL;
				}
			}
		}
		if (pairNumGenerated > 0) {
			if (mixType == 1) {
				mixBuffersRingMix(partialBuf, pairBuf, pairNumGenerated);
			} else {
				mixBuffersRing(partialBuf, pairBuf, pairNumGenerated);
			}
		}
		if (numGenerated > pairNumGenerated) {
			if (mixType == 1) {
				mixBuffersRingMix(partialBuf + pairNumGenerated, NULL, numGenerated - pairNumGenerated);
			} else {
				mixBuffersRing(partialBuf + pairNumGenerated, NULL, numGenerated - pairNumGenerated);
			}
		}
	}

	for (unsigned int i = 0; i < numGenerated; i++) {
		*leftBuf++ = *partialBuf * stereoVolume.leftVol;
	}
	for (unsigned int i = 0; i < numGenerated; i++) {
		*rightBuf++ = *partialBuf * stereoVolume.rightVol;
	}
	while (numGenerated < length) {
		*leftBuf++ = 0.0f;
		*rightBuf++ = 0.0f;
		numGenerated++;
	}
	return true;
}

bool Partial::shouldReverb() {
	if (!isActive()) {
		return false;
	}
	return patchCache->reverb;
}

void Partial::startDecayAll() {
	tva->startDecay();
	tvp->startDecay();
	tvf->startDecay();
}
