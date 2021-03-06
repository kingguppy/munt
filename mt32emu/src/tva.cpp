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

/*
Some notes on this class:

la32TargetAmp and la32AmpIncrement represent memory-mapped LA32 registers in the real devices.
The values that we set them to correspond exactly to the values that the real control ROM sets
(according to Mok's specifications, and assuming no bugs in our implementation).

Our interpretation of these values is partly based on guesswork and sample analysis.
Here's what we're pretty confident about:
 - The most significant bit of la32AmpIncrement indicates the direction that the LA32's current internal amp value (currentAmp in our emulation) should change in.
   Set means downward, clear means upward.
 - The lower 7 bits of la32AmpIncrement indicate how quickly currentAmp should be changed.
 - la32AmpIncrement is 0, currentAmp is set immediately to the equivalent of la32TargetAmp and no interrupt is raised.
 - Otherwise, if the MSb is set:
    - If currentAmp already corresponds to a value <= la32TargetAmp, currentAmp is set immediately to the equivalent of la32TargetAmp and an interrupt is raised.
    - Otherwise, currentAmp is gradually reduced (at a rate determined by the lower 7 bits of la32AmpIncrement), and once it reaches the equivalent of la32TargetAmp an interrupt is raised.
 - Otherwise (the MSb is unset):
    - If currentAmp already corresponds to a value >= la32TargetAmp, currentAmp is set immediately to the equivalent of la32TargetAmp and an interrupt is raised.
    - Otherwise, currentAmp is gradually increased (at a rate determined by the lower 7 bits of la32AmpIncrement), and once it reaches the equivalent of la32TargetAmp an interrupt is raised.
We're emulating what happens when the interrupt is raised in "nextPhase()".
*/
#include <cmath>

#include "mt32emu.h"
#include "mmath.h"

namespace MT32Emu {

// FIXME: Need to confirm that this is correct. Sounds about right.
const int TVA_TARGET_AMP_MULT = 0x800000;
const int MAX_CURRENT_AMP = 0xFF * TVA_TARGET_AMP_MULT;

// CONFIRMED: Matches a table in ROM - haven't got around to coming up with a formula for it yet.
static Bit8u biasLevelToAmpSubtractionCoeff[13] = {255, 187, 137, 100, 74, 54, 40, 29, 21, 15, 10, 5, 0};

TVA::TVA(const Partial *usePartial) :
	partial(usePartial), system(&usePartial->getSynth()->mt32ram.system) {
}

void TVA::setAmpIncrement(Bit8u newAmpIncrement) {
	la32AmpIncrement = newAmpIncrement;

	largeAmpInc = newAmpIncrement & 0x7F;
	// FIXME: We could use a table for this in future
	largeAmpInc = (unsigned int)(EXP10F((largeAmpInc - 1) / 26.0f) * 256.0f);
}

float TVA::nextAmp() {
	// FIXME: This whole method is based on guesswork
	Bit32u target = la32TargetAmp * TVA_TARGET_AMP_MULT;
	if (la32AmpIncrement == 0) {
		currentAmp = target;
	} else {
		if ((la32AmpIncrement & 0x80) != 0) {
			// Lowering amp
			if (largeAmpInc > currentAmp) {
				currentAmp = target;
				nextPhase();
			} else {
				currentAmp -= largeAmpInc;
				if (currentAmp <= target) {
					currentAmp = target;
					nextPhase();
				}
			}
		} else {
			// Raising amp
			if (MAX_CURRENT_AMP - currentAmp < largeAmpInc) {
				currentAmp = target;
				nextPhase();
			} else {
				currentAmp += largeAmpInc;
				if (currentAmp >= target) {
					currentAmp = target;
					nextPhase();
				}
			}
		}
	}
	// FIXME:KG: Note that the "65536.0f" here is slightly arbitrary, and needs to be confirmed. 32768.0f is more likely.
	// FIXME:KG: We should perhaps use something faster once we've got the details sorted out, but the real synth's amp level changes pretty smoothly.
	return EXP2F((float)currentAmp / TVA_TARGET_AMP_MULT / 16.0f - 1.0f) / 65536.0f;
}

static int multBias(Bit8u biasLevel, int bias) {
	return (bias * biasLevelToAmpSubtractionCoeff[biasLevel]) >> 5;
}

static int calcBiasAmpSubtraction(Bit8u biasPoint, Bit8u biasLevel, int key) {
	if ((biasPoint & 0x40) == 0) {
		int bias = biasPoint + 33 - key;
		if (bias > 0) {
			return multBias(biasLevel, bias);
		}
	} else {
		int bias = biasPoint - 31 - key;
		if (bias < 0) {
			bias = -bias;
			return multBias(biasLevel, bias);
		}
	}
	return 0;
}

static int calcBiasAmpSubtractions(const TimbreParam::PartialParam *partialParam, int key) {
	int biasAmpSubtraction1 = calcBiasAmpSubtraction(partialParam->tva.biasPoint1, partialParam->tva.biasLevel1, key);
	if (biasAmpSubtraction1 > 255) {
		return 255;
	}
	int biasAmpSubtraction2 = calcBiasAmpSubtraction(partialParam->tva.biasPoint2, partialParam->tva.biasLevel2, key);
	if (biasAmpSubtraction2 > 255) {
		return 255;
	}
	int biasAmpSubtraction = biasAmpSubtraction1 + biasAmpSubtraction2;
	if (biasAmpSubtraction > 255) {
		return 255;
	}
	return biasAmpSubtraction;
}

static int calcVeloAmpSubtraction(Bit8u veloSensitivity, unsigned int velocity) {
	// FIXME:KG: Better variable names
	int velocityMult = veloSensitivity - 50;
	int absVelocityMult = velocityMult < 0 ? -velocityMult : velocityMult;
	velocityMult = (signed)((unsigned)(velocityMult * ((signed)velocity - 64)) << 2);
	return absVelocityMult - (velocityMult >> 8); // PORTABILITY NOTE: Assumes arithmetic shift
}

static int calcBasicAmp(const Tables *tables, const Partial *partial, const MemParams::System *system, const TimbreParam::PartialParam *partialParam, const MemParams::PatchTemp *patchTemp, const MemParams::RhythmTemp *rhythmTemp, int biasAmpSubtraction, int veloAmpSubtraction, Bit8u expression) {
	int amp = 155;

	if (!partial->isRingModulatingSlave()) {
		amp -= tables->masterVolToAmpSubtraction[system->masterVol];
		if (amp < 0) {
			return 0;
		}
		amp -= tables->levelToAmpSubtraction[patchTemp->outputLevel];
		if (amp < 0) {
			return 0;
		}
		amp -= tables->levelToAmpSubtraction[expression];
		if (amp < 0) {
			return 0;
		}
		if (rhythmTemp != NULL) {
			amp -= tables->levelToAmpSubtraction[rhythmTemp->outputLevel];
			if (amp < 0) {
				return 0;
			}
		}
	}
	amp -= biasAmpSubtraction;
	if (amp < 0) {
		return 0;
	}
	amp -= tables->levelToAmpSubtraction[partialParam->tva.level];
	if (amp < 0) {
		return 0;
	}
	amp -= veloAmpSubtraction;
	if (amp < 0) {
		return 0;
	}
	if (amp > 155) {
		amp = 155;
	}
	amp -= partialParam->tvf.resonance >> 1;
	if (amp < 0) {
		return 0;
	}
	return amp;
}

int calcKeyTimeSubtraction(Bit8u envTimeKeyfollow, int key) {
	if (envTimeKeyfollow == 0) {
		return 0;
	}
	return (key - 60) >> (5 - envTimeKeyfollow); // PORTABILITY NOTE: Assumes arithmetic shift
}

void TVA::reset(const Part *newPart, const TimbreParam::PartialParam *newPartialParam, const MemParams::RhythmTemp *newRhythmTemp) {
	part = newPart;
	partialParam = newPartialParam;
	patchTemp = newPart->getPatchTemp();
	rhythmTemp = newRhythmTemp;

	playing = true;

	Tables *tables = &partial->getSynth()->tables;

	int key = partial->getPoly()->getKey();
	int velocity = partial->getPoly()->getVelocity();

	keyTimeSubtraction = calcKeyTimeSubtraction(partialParam->tva.envTimeKeyfollow, key);

	biasAmpSubtraction = calcBiasAmpSubtractions(partialParam, key);
	veloAmpSubtraction = calcVeloAmpSubtraction(partialParam->tva.veloSensitivity, velocity);

	int newTargetAmp = calcBasicAmp(tables, partial, system, partialParam, patchTemp, newRhythmTemp, biasAmpSubtraction, veloAmpSubtraction, part->getExpression());

	if (partialParam->tva.envTime[0] == 0) {
		// Initially go to the TVA_PHASE_ATTACK target amp, and spend the next phase going from there to the TVA_PHASE_2 target amp
		// Note that this means that velocity never affects time for this partial.
		newTargetAmp += partialParam->tva.envLevel[0];
		targetPhase = TVA_PHASE_2 - 1; // The first target used in nextPhase() will be TVA_PHASE_2
	} else {
		// Initially go to the base amp determined by TVA level, part volume, etc., and spend the next phase going from there to the full TVA_PHASE_ATTACK target amp.
		targetPhase = TVA_PHASE_ATTACK - 1; // The first target used in nextPhase() will be TVA_PHASE_ATTACK
	}

	// "Go downward as quickly as possible".
	// Since currentAmp is 0, nextAmp() will notice that we're already at or below the target and trying to go downward,
	// and therefore jump to the target immediately and call nextPhase().
	setAmpIncrement(0x80 | 127);
	la32TargetAmp = (Bit8u)newTargetAmp;

	currentAmp = 0;
}

void TVA::startDecay() {
	if (targetPhase >= TVA_PHASE_RELEASE) {
		return;
	}
	targetPhase = TVA_PHASE_RELEASE; // The next time nextPhase() is called, it will think TVA_PHASE_RELEASE has finished and the partial will be aborted
	if (partialParam->tva.envTime[4] == 0) {
		setAmpIncrement(1);
	} else {
		setAmpIncrement(-partialParam->tva.envTime[4]);
	}
	la32TargetAmp = 0;
}

void TVA::recalcSustain() {
	// We get pinged periodically by the pitch code to recalculate our values when in sustain.
	// This is done so that the TVA will respond to things like MIDI expression and volume changes while it's sustaining, which it otherwise wouldn't do.

	// The check for envLevel[3] == 0 strikes me as slightly dumb. FIXME: Explain why
	if (targetPhase != TVA_PHASE_SUSTAIN || partialParam->tva.envLevel[3] == 0) {
		return;
	}
	// We're sustaining. Recalculate all the values
	Tables *tables = &partial->getSynth()->tables;
	int newTargetAmp = calcBasicAmp(tables, partial, system, partialParam, patchTemp, rhythmTemp, biasAmpSubtraction, veloAmpSubtraction, part->getExpression());
	newTargetAmp += partialParam->tva.envLevel[3];
	// FIXME: This whole concept seems flawed. We don't really know what the *actual* amp value is, right? It may well not be la32TargetAmp yet (unless I've missed something). So we could end up going in the wrong direction...
	int ampDelta = newTargetAmp - la32TargetAmp;

	// Calculate an increment to get to the new amp value in a short, more or less consistent amount of time
	if (ampDelta >= 0) {
		setAmpIncrement(tables->envLogarithmicTime[(Bit8u)ampDelta] - 2);
	} else {
		setAmpIncrement((tables->envLogarithmicTime[(Bit8u)-ampDelta] - 2) | 0x80);
	}
	la32TargetAmp = newTargetAmp;
	// Configure so that once the transition's complete and nextPhase() is called, we'll just re-enter sustain phase (or decay phase, depending on parameters at the time).
	targetPhase = TVA_PHASE_SUSTAIN - 1;
}

bool TVA::isPlaying() const {
	return playing;
}

int TVA::getPhase() const {
	return targetPhase;
}

void TVA::nextPhase() {
	Tables *tables = &partial->getSynth()->tables;

	if (targetPhase >= TVA_PHASE_DEAD || !playing) {
		partial->getSynth()->printDebug("TVA::nextPhase(): Shouldn't have got here with targetPhase %d, playing=%s", targetPhase, playing ? "true" : "false");
		return;
	}
	targetPhase++;

	if (targetPhase == TVA_PHASE_DEAD) {
		playing = false;
		return;
	}

	bool allLevelsZeroFromNowOn = false;
	if (partialParam->tva.envLevel[3] == 0) {
		if (targetPhase == TVA_PHASE_4) {
			allLevelsZeroFromNowOn = true;
		} else if (partialParam->tva.envLevel[2] == 0) {
			if (targetPhase == TVA_PHASE_3) {
				allLevelsZeroFromNowOn = true;
			} else if (partialParam->tva.envLevel[1] == 0) {
				if (targetPhase == TVA_PHASE_2) {
					allLevelsZeroFromNowOn = true;
				} else if (partialParam->tva.envLevel[0] == 0) {
					if (targetPhase == TVA_PHASE_ATTACK)  { // this line added, missing in ROM - FIXME: Add description of repercussions
						allLevelsZeroFromNowOn = true;
					}
				}
			}
		}
	}

	int newTargetAmp;
	int newAmpIncrement;
	int envPointIndex = targetPhase - 1;

	if (!allLevelsZeroFromNowOn) {
		newTargetAmp = calcBasicAmp(tables, partial, system, partialParam, patchTemp, rhythmTemp, biasAmpSubtraction, veloAmpSubtraction, part->getExpression());

		if (targetPhase == TVA_PHASE_SUSTAIN || targetPhase == TVA_PHASE_RELEASE) {
			if (partialParam->tva.envLevel[3] == 0) {
				playing = false;
				return;
			}
			if (!partial->getPoly()->canSustain()) {
				targetPhase = TVA_PHASE_RELEASE;
				newTargetAmp = 0;
				newAmpIncrement = -partialParam->tva.envTime[4];
				if (newAmpIncrement >= 0) {
					// FIXME: This must mean newAmpIncrement was 0, and we're now making it 1, which makes us go in the wrong direction. WTF?
					newAmpIncrement++;
				}
			} else {
				newTargetAmp += partialParam->tva.envLevel[3];
				newAmpIncrement = 0;
			}
		} else {
			newTargetAmp += partialParam->tva.envLevel[envPointIndex];
		}
	} else {
		newTargetAmp = 0;
	}

	if ((targetPhase != TVA_PHASE_SUSTAIN && targetPhase != TVA_PHASE_RELEASE) || allLevelsZeroFromNowOn) {
		int envTimeSetting = partialParam->tva.envTime[envPointIndex];

		if (targetPhase == TVA_PHASE_ATTACK) {
			envTimeSetting -= ((signed)partial->getPoly()->getVelocity() - 64) >> (6 - partialParam->tva.envTimeVeloSensitivity); // PORTABILITY NOTE: Assumes arithmetic shift

			if (envTimeSetting <= 0 && partialParam->tva.envTime[envPointIndex] != 0) {
				envTimeSetting = 1;
			}
		} else {
			envTimeSetting -= keyTimeSubtraction;
		}
		if (envTimeSetting > 0) {
			int ampDelta = newTargetAmp - la32TargetAmp;
			if (ampDelta <= 0) {
				if (ampDelta == 0) {
					// la32TargetAmp and newTargetAmp are the same.
					// We'd never get anywhere if we used these parameters, so instead make la32TargetAmp one less than it really should be and set ampDelta accordingly
					ampDelta--; // i.e. ampDelta = -1;
					newTargetAmp--;
					if (newTargetAmp < 0) {
						// Oops, newTargetAmp is less than zero now, so let's do it the other way:
						// Make newTargetAmp one more than it really should've been and set ampDelta accordingly
						// FIXME: This means ampDelta will be positive just below here where it's inverted, and we'll end up using envLogarithmicTime[-1], and we'll be setting newAmpIncrement to be descending later on, etc..
						ampDelta = -ampDelta; // i.e. ampDelta = 1;
						newTargetAmp = -newTargetAmp;
					}
				}
				ampDelta = -ampDelta;
				newAmpIncrement = tables->envLogarithmicTime[(Bit8u)ampDelta] - envTimeSetting;
				if (newAmpIncrement <= 0) {
					newAmpIncrement = 1;
				}
				newAmpIncrement = newAmpIncrement | 0x80;
			} else {
				// FIXME: The last 22 or so entries in this table are 128 - surely that fucks things up, since that ends up being -128 signed?
				newAmpIncrement = tables->envLogarithmicTime[(Bit8u)ampDelta] - envTimeSetting;
				if (newAmpIncrement <= 0) {
					newAmpIncrement = 1;
				}
			}
		} else {
			// FIXME: Shouldn't we be ensuring that la32TargetAmp != newTargetAmp here?
			newAmpIncrement = newTargetAmp >= la32TargetAmp ? (0x80 | 127) : 127;
		}

		// FIXME: What's the point of this? It's checked or set to non-zero everywhere above
		if (newAmpIncrement == 0) {
			newAmpIncrement = 1;
		}
	}

	la32TargetAmp = (Bit8u)newTargetAmp;
	setAmpIncrement((Bit8u)newAmpIncrement);
}

}
