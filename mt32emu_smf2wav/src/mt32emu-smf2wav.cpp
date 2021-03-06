/*
 * Copyright (C) 2009, 2011 Jerome Fisher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <mt32emu/mt32emu.h>

#include "config.h"
#include "smf.h"

static bool quiet = false;

static const int DEFAULT_BUFFER_SIZE = 512 * 1024;
static const int DEFAULT_SAMPLE_RATE = 32000;

static const int HEADEROFFS_RIFFLEN = 4;
static const int HEADEROFFS_SAMPLERATE = 24;
static const int HEADEROFFS_BYTERATE = 28;
static const int HEADEROFFS_DATALEN = 40;

static long secondsToSamples(double seconds, int sampleRate) {
	return seconds * sampleRate;
}

static bool writeWAVEHeader(FILE *dstFile, int sampleRate) {
	int byteRate = sampleRate * 4;
	// All values are little-endian
	unsigned char waveHeader[] = {
		'R','I','F','F',
		0,0,0,0, // Length to be filled in later
		'W','A','V','E',

		// "fmt " chunk
		'f','m','t',' ',
		0x10, 0x00, 0x00, 0x00, // 0x00000010 - 16 byte chunk
		0x01, 0x00, // 0x0001 - PCM/Uncompressed
		0x02, 0x00, // 0x0002 - 2 channels
		0x00, 0x7D, 0x00, 0x00, // 0x00007D00 - 32kHz, overwritten by real sample rate below
		0x00, 0xF4, 0x01, 0x00, // 0x0001F400 - 128000 bytes/sec, overwritten with real value below
		0x04, 0x00, // 0x0004 - 4 byte alignment
		0x10, 0x00, // 0x0010 - 16 bits/sample

		// "data" chunk
		'd','a','t','a',
		0x00, 0x00, 0x00, 0x00 // Chunk length, to be filled in later
	};
	waveHeader[HEADEROFFS_SAMPLERATE] = sampleRate & 0xFF;
	waveHeader[HEADEROFFS_SAMPLERATE + 1] = (sampleRate >> 8) & 0xFF;
	waveHeader[HEADEROFFS_SAMPLERATE + 2] = (sampleRate >> 16) & 0xFF;
	waveHeader[HEADEROFFS_SAMPLERATE + 3] = (sampleRate >> 24) & 0xFF;
	waveHeader[HEADEROFFS_BYTERATE] = byteRate & 0xFF;
	waveHeader[HEADEROFFS_BYTERATE + 1] = (byteRate >> 8) & 0xFF;
	waveHeader[HEADEROFFS_BYTERATE + 2] = (byteRate >> 16) & 0xFF;
	waveHeader[HEADEROFFS_BYTERATE + 3] = (byteRate >> 24) & 0xFF;
	return fwrite(waveHeader, 1, sizeof(waveHeader), dstFile) == sizeof(waveHeader);
}

static bool fillWAVESizes(FILE *dstFile, int numSamples) {
	// FIXME: Check return codes, etc.
	int dataSize = numSamples * 4;
	int riffSize = dataSize + 28;
	if (fseek(dstFile, HEADEROFFS_RIFFLEN, SEEK_SET))
		return false;
	if (fputc(riffSize & 0xFF, dstFile) == EOF)
		return false;
	if (fputc((riffSize >> 8) & 0xFF, dstFile) == EOF)
		return false;
	if (fputc((riffSize >> 16) & 0xFF, dstFile) == EOF)
		return false;
	if (fputc((riffSize >> 24) & 0xFF, dstFile) == EOF)
		return false;
	if (fseek(dstFile, HEADEROFFS_DATALEN, SEEK_SET))
		return false;
	if (fputc(dataSize & 0xFF, dstFile) == EOF)
		return false;
	if (fputc((dataSize >> 8) & 0xFF, dstFile) == EOF)
		return false;
	if (fputc((dataSize >> 16) & 0xFF, dstFile) == EOF)
		return false;
	if (fputc((dataSize >> 24) & 0xFF, dstFile) == EOF)
		return false;
	return true;
}

static long getFileLength(FILE *file) {
	long oldPos = ftell(file);
	if (oldPos == -1)
		return -1;
	if (fseek(file, 0, SEEK_END))
		return -1;
	long len = ftell(file);
	if (fseek(file, oldPos, SEEK_SET))
		return -1;
	return len;
}

static bool playSysexFile(MT32Emu::Synth *synth, char *syxFileName) {
	bool ok = false;
	MT32Emu::Bit8u *syxbuf;
	FILE *syxFile = fopen(syxFileName, "rb");
	if (syxFile != NULL) {
		// FIXME: Add error checking
		long len = getFileLength(syxFile);
		if (len != -1) {
			syxbuf = (MT32Emu::Bit8u*)malloc(len);
			if (syxbuf != NULL) {
				if (fread(syxbuf, 1, len, syxFile) == (unsigned)len) {
					long start = -1;
					ok = true;
					for (long i = 0; i < len; i++) {
						if (syxbuf[i] == 0xF0) {
							if (start != -1) {
								fprintf(stderr, "Started a new sysex message before the last finished - sysex file '%s' may be in an unsupported format.\n", syxFileName);
							}
							start = i;
						}
						else if (syxbuf[i] == 0xF7) {
							if (start == -1) {
								fprintf(stderr, "Ended a sysex message without a start byte - sysex file '%s' may be in an unsupported format.\n", syxFileName);
							} else {
								synth->playSysex((MT32Emu::Bit8u*)(syxbuf + start), i - start + 1);
							}
							start = -1;
						}
					}
				} else {
					fprintf(stderr, "Error reading %ld bytes from sysex file '%s'.\n", len, syxFileName);
				}
				free(syxbuf);
			} else {
				fprintf(stderr, "Error allocating %ld bytes for sysex file '%s'.\n", len, syxFileName);
			}
		} else {
			fprintf(stderr, "Error getting length of sysex file '%s'.\n", syxFileName);
		}
		fclose(syxFile);
	} else {
		fprintf(stderr, "Error opening sysex file '%s' for reading.\n", syxFileName);
	}
	return ok;
}

/**
 * Render numSamples samples to the buffer.
 * bufferSampleSize determines the maximum number of samples to be rendered by the emulator in one pass.
 * This can have a big impact on performance (more at a time=better).
 */
static unsigned int render(MT32Emu::Synth *synth, MT32Emu::Bit16s sampleBuffer[], unsigned int bufferSampleSize, FILE *dstFile, unsigned int numSamples, bool &waitingForNoise) {
	unsigned int skippedSamples = 0;
	unsigned int renderedSamples = numSamples;
	while (numSamples > 0) {
		unsigned int renderedSamplesThisPass = numSamples > bufferSampleSize ? bufferSampleSize : numSamples;
		synth->render(sampleBuffer, renderedSamplesThisPass);
		for (unsigned int i = 0; i < renderedSamplesThisPass; i++) {
			if (waitingForNoise) {
				if (sampleBuffer[i] == 0 && sampleBuffer[i + 1] == 0) {
					skippedSamples++;
					continue;
				}
				waitingForNoise = false;
			}
			fputc(sampleBuffer[i * 2] & 0xFF, dstFile);
			fputc((sampleBuffer[i * 2] >> 8) & 0xFF, dstFile);
			fputc(sampleBuffer[i * 2 + 1] & 0xFF, dstFile);
			fputc((sampleBuffer[i * 2 + 1] >> 8) & 0xFF, dstFile);
		}
		numSamples -= renderedSamplesThisPass;
	}
	return renderedSamples - skippedSamples;
}

static void processSMF(char *syxFileName, smf_t *smf, char *dstFileName, MT32Emu::SynthProperties &synthProperties, unsigned int bufferSize, unsigned int endAfter, bool renderUntilInactive, bool recordInitialSilence) {
	MT32Emu::Synth *synth = new MT32Emu::Synth();
	MT32Emu::Bit16s *sampleBuffer = NULL;
	FILE *dstFile;
	bool waitingForNoise = !recordInitialSilence;
	if (synth->open(synthProperties)) {
		if (syxFileName != NULL) {
			playSysexFile(synth, syxFileName);
		}

		dstFile = fopen(dstFileName, "wb");
		if (dstFile != NULL) {
			if (writeWAVEHeader(dstFile, synthProperties.sampleRate)) {
				int unterminatedSysexLen = 0;
				unsigned char *unterminatedSysex = NULL;
				unsigned long renderedSamples = 0;
				unsigned long writtenSamples = 0;
				for (;;) {
					smf_event_t *event = smf_get_next_event(smf);
					unsigned long eventSampleIx;

					if (event == NULL) {
						break;
					}

					assert(event->track->track_number >= 0);

					eventSampleIx = secondsToSamples(event->time_seconds, synthProperties.sampleRate);
					if (eventSampleIx < renderedSamples) {
						fprintf(stderr, "Event went back in time!\n");
					} else {
						if (sampleBuffer == NULL) {
							sampleBuffer = new MT32Emu::Bit16s[bufferSize / 2];
						}
						if (eventSampleIx > endAfter) {
							eventSampleIx = endAfter;
						}
						unsigned int renderLength = eventSampleIx - renderedSamples;
						writtenSamples += render(synth, sampleBuffer, bufferSize / 4, dstFile, renderLength, waitingForNoise);
						renderedSamples += renderLength;
						if (eventSampleIx == endAfter) {
							break;
						}
					}

					if (smf_event_is_metadata(event)) {
						char *decoded = smf_event_decode(event);
						if (decoded && !quiet)
							fprintf(stdout, "Metadata: %s\n", decoded);
					} else if (smf_event_is_sysex(event) || smf_event_is_sysex_continuation(event))  {
						bool unterminated = smf_event_is_unterminated_sysex(event);
						bool addUnterminated = unterminated;
						bool continuation = smf_event_is_sysex_continuation(event);
						unsigned char *buf;
						int len;
						if (continuation) {
							if (unterminatedSysex != NULL) {
								addUnterminated = true;
							} else {
								fprintf(stderr, "Sysex continuation received without preceding unterminated sysex - hoping for the best\n");
							}
							buf = event->midi_buffer + 1;
							len = event->midi_buffer_length - 1;
						} else {
							if (unterminatedSysex != NULL) {
								fprintf(stderr, "New sysex received with an unterminated sysex pending - ignoring unterminated\n");
								delete[] unterminatedSysex;
								unterminatedSysex = NULL;
								unterminatedSysexLen = 0;
							}
							buf = event->midi_buffer;
							len = event->midi_buffer_length;
						}
						if (addUnterminated) {
							unsigned char *newUnterminatedSysex = new unsigned char[unterminatedSysexLen + len];
							if(unterminatedSysex != NULL) {
								memcpy(newUnterminatedSysex, unterminatedSysex, unterminatedSysexLen);
								delete[] unterminatedSysex;
							}
							memcpy(newUnterminatedSysex + unterminatedSysexLen, buf, len);
							unterminatedSysex = newUnterminatedSysex;
							unterminatedSysexLen += len;
							buf = unterminatedSysex;
							len = unterminatedSysexLen;
						}
						if (!unterminated) {
							synth->playSysex(buf, len);
							if (addUnterminated) {
								delete[] unterminatedSysex;
								unterminatedSysex = NULL;
								unterminatedSysexLen = 0;
							}
						}
					} else {
						if (event->midi_buffer_length > 3) {
							fprintf(stderr, "Got message with unusual length: %d\n", event->midi_buffer_length);
							for (int i = 0; i < event->midi_buffer_length; i++) {
								fprintf(stderr, " %02x", event->midi_buffer[i]);
							}
							fprintf(stderr, "\n");
						} else {
							MT32Emu::Bit32u msg = 0;
							for (int i = 0; i < event->midi_buffer_length; i++) {
								msg |= (event->midi_buffer[i] << (8 * i));
							}
							synth->playMsg(msg);
						}
					}
				}
				if (renderUntilInactive) {
					while (renderedSamples < endAfter && synth->isActive()) {
						// FIXME: Very inefficient, perhaps we should add a renderWhileActive() to Synth.
						MT32Emu::Bit16s tmpBuffer[2];
						writtenSamples += render(synth, tmpBuffer, 1, dstFile, 1, waitingForNoise);
						renderedSamples++;
					}
				}
				delete[] unterminatedSysex;
				if (!fillWAVESizes(dstFile, writtenSamples)) {
					fprintf(stderr, "Error writing final sizes to WAVE header\n");
				}
			} else {
				fprintf(stderr, "Error writing WAVE header to '%s'\n", dstFileName);
			}
			fclose(dstFile);
		} else {
			fprintf(stderr, "Error opening file '%s' for writing.\n", dstFileName);
		}
	} else {
		fprintf(stderr, "Error opening MT32Emu synthesizer.\n");
	}
	delete[] sampleBuffer;
	delete synth;
}

static void printVersion(void) {
	fprintf(stdout, "%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

static void printUsage(char *cmd) {
	printVersion();
	fprintf(stdout, "\nusage: %s [arguments] <SMF MIDI file>\n\n", cmd);
	fprintf(stdout, "Arguments:\n");
	fprintf(stdout, " -a              Record silent samples at the start of the render\n");
	fprintf(stdout, " -b              Buffer size (in bytes) (minimum: 4, default: %d)\n", DEFAULT_BUFFER_SIZE);
	fprintf(stdout, " -e              End after rendering at most this many samples. 0=unlimited (default: 0)\n");
	fprintf(stdout, " -f              Force overwrite of output file if already present\n");
	fprintf(stdout, " -h              Show this help and exit\n");
	fprintf(stdout, " -o <filename>   Output file (default: source file name with \".wav\" appended)\n");
	fprintf(stdout, " -q              Be quiet\n");
	fprintf(stdout, " -r <samplerate> Set the sample rate (in Hz) (default: %d)\n", DEFAULT_SAMPLE_RATE);
	fprintf(stdout, " -s <filename>   Sysex file to play before the SMF file\n");
	fprintf(stdout, " -t              Don't render until the synth becomes inactive - stop once the SMF has ended");
}

int main(int argc, char *argv[]) {
	bool force = false;
	int ch;
	int rc = 0;
	char *syxFileName = NULL, *dstFileNameArg = NULL;
	smf_t *smf = NULL;
	char *cmd = argv[0];
	unsigned int bufferSize = DEFAULT_BUFFER_SIZE;
	unsigned int sampleRate = DEFAULT_SAMPLE_RATE;
	unsigned int endAfter = UINT_MAX;
	bool renderUntilInactive = true;
	bool recordInitialSilence = false;

	while ((ch = getopt(argc, argv, "ab:e:fho:qr:s:t")) != -1) {
		switch (ch) {
		case 'a':
			recordInitialSilence = true;
			break;
		case 'b':
			bufferSize = atoi(optarg);
			if (bufferSize < 4) {
				printUsage(cmd);
				return 0;
			}
			break;
		case 'e':
			endAfter = atoi(optarg);
			if (endAfter == 0) {
				endAfter = UINT_MAX;
			}
			break;
		case 'f':
			force = true;
			break;
		case 'o':
			dstFileNameArg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			sampleRate = atoi(optarg);
			break;
		case 's':
			syxFileName = optarg;
			break;
		case 't':
			renderUntilInactive = false;
			break;
		case '?':
		case 'h':
		default:
			printUsage(cmd);
			return 0;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		fprintf(stderr, "Wrong number of non-option arguments (%d given, 1 required)\n", argc);
		printUsage(cmd);
		return -1;
	}
	char *srcFileName = argv[0];
	if (srcFileName == NULL) {
		fprintf(stderr, "No source file name given.\n");
		printUsage(cmd);
		return -1;
	}

	char *dstFileName;
	if (dstFileNameArg != NULL) {
		dstFileName = dstFileNameArg;
	} else {
		dstFileName = (char *)malloc(strlen(srcFileName) + 5);
		if(dstFileName == NULL) {
			fprintf(stderr, "Error allocating %lu bytes for destination filename.\n", (unsigned long)strlen(srcFileName) + 5);
			return -1;
		}
		sprintf(dstFileName, "%s.wav", srcFileName);
	}

	if (!force) {
		// FIXME: Lame way of checking whether the file exists
		FILE *testDstFile;
		if (dstFileName != NULL && (testDstFile = fopen(dstFileName, "rb")) != NULL) {
			fclose(testDstFile);
			fprintf(stderr, "Destination file '%s' exists.\n", dstFileName);
			goto cleanup;
		}
	}

	smf = smf_load(srcFileName);
	if (smf != NULL) {
		if (!quiet) {
			char *decoded = smf_decode(smf);
			fprintf(stdout, "%s.\n", decoded);
			free(decoded);
		}
		assert(smf->number_of_tracks >= 1);
		MT32Emu::SynthProperties synthProperties = {0};
		synthProperties.sampleRate = sampleRate;
		synthProperties.useReverb = true;
		synthProperties.useDefaultReverb = true;
		processSMF(syxFileName, smf, dstFileName, synthProperties, bufferSize, endAfter, renderUntilInactive, recordInitialSilence);
		smf_delete(smf);
	} else {
		fprintf(stderr, "Error parsing SMF file '%s'.\n", srcFileName);
		rc = -1;
	}

cleanup:
	if(dstFileNameArg == NULL && dstFileName != NULL) {
		free(dstFileName);
	}
	return rc;
}

