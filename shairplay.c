/**
 *  Copyright (C) 2012-2013  Juho Vähä-Herttua
 *
 *  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
 *  "Software"), to deal in the Software without restriction, including
 *  without limitation the rights to use, copy, modify, merge, publish,
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included
 *  in all copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>


#include <shairplay/dnssd.h>
#include <shairplay/raop.h>

#include <alsa/asoundlib.h>

//#include <ao/ao.h>

#define VERSION "aki5-butchered"

typedef struct {
	snd_pcm_t *pcmdev;

	int buffering;
	int buflen;
	char buffer[8192];

	float volume;
} shairplay_session_t;


static int running;


static void
signal_handler(int sig)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		running = 0;
		break;
	}
}
static void
init_signals(void)
{
	struct sigaction sigact;

	sigact.sa_handler = signal_handler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
}

static snd_pcm_t *
audio_open_device(int bits, int channels, int samplerate)
{
	snd_pcm_t *pcm;
	snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);

	snd_pcm_hw_params_t *hw_params;
	snd_pcm_hw_params_malloc(&hw_params);
	snd_pcm_hw_params_any(pcm, hw_params);
	snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_channels(pcm, hw_params, 2);
	snd_pcm_hw_params_set_rate(pcm, hw_params, 44100, 0);
	// 10 buffers of 10 milliseconds
	snd_pcm_hw_params_set_periods(pcm, hw_params, 10, 0);
	snd_pcm_hw_params_set_period_time(pcm, hw_params, 100*1000, 0);
	snd_pcm_hw_params(pcm, hw_params);
	snd_pcm_hw_params_free(hw_params);

	return pcm;
}

static void *
audio_init(void *cls, int bits, int channels, int samplerate)
{
	shairplay_session_t *session;

	session = calloc(1, sizeof(shairplay_session_t));
	assert(session);

	session->pcmdev = audio_open_device(bits, channels, samplerate);
	if (session->pcmdev == NULL) {
		printf("Error opening device %d\n", errno);
		printf("The device might already be in use");
	}

	session->buffering = 1;
	session->volume = 1.0f;
	return session;
}

static int
audio_output(shairplay_session_t *session, const void *buffer, int buflen)
{
	struct {
		short left;
		short right;
	} frames[1024];

	int nbytes = (buflen < sizeof frames) ? buflen : sizeof frames;
	memcpy(frames, buffer, nbytes);

	int nframes = nbytes / sizeof frames[0];
	for(int i = 0; i < nframes; i++){
		int sum = (frames[i].left + frames[i].right) / 2;
		frames[i].left = sum;
		frames[i].right = sum;
	}

	if(session->pcmdev != NULL){
		int nwr = snd_pcm_writei(session->pcmdev, frames, nframes);
		if(nwr == -EPIPE){
			fprintf(stderr, "pcmdev underrun\n");
			session->buffering = 1;
		}
		return 4*nwr;
	}

	return buflen;

}

static void
audio_process(void *cls, void *opaque, const void *buffer, int buflen)
{
	shairplay_session_t *session = opaque;
	int processed;

	if (session->buffering) {
		printf("Buffering...\n");
		if (session->buflen+buflen < sizeof(session->buffer)) {
			memcpy(session->buffer+session->buflen, buffer, buflen);
			session->buflen += buflen;
			return;
		}
		session->buffering = 0;
		printf("Finished buffering...\n");

		processed = 0;
		while (processed < session->buflen) {
			processed += audio_output(session,
			                          session->buffer+processed,
			                          session->buflen-processed);
		}
		session->buflen = 0;
	}

	processed = 0;
	while (processed < buflen) {
		processed += audio_output(session,
		                          buffer+processed,
		                          buflen-processed);
	}
}

static void
audio_destroy(void *cls, void *opaque)
{
	shairplay_session_t *session = opaque;

	if(session->pcmdev != NULL){
		snd_pcm_drain(session->pcmdev);
		snd_pcm_close(session->pcmdev);
	}
	free(session);
}

static void
audio_set_volume(void *cls, void *opaque, float volume)
{
	shairplay_session_t *session = opaque;
	session->volume = pow(10.0, 0.05*volume);
}

void
audio_set_progress(void *arg1, void *arg2, unsigned int start, unsigned int curr, unsigned int end)
{
	fprintf(stderr, "progress %u/%u/%u\n", start, curr, end);
}

int
main(int argc, char *argv[])
{

	dnssd_t *dnssd;
	raop_t *raop;
	raop_callbacks_t raop_cbs;

	int error;

	init_signals();

	snd_pcm_t *pcmdev = audio_open_device(16, 2, 44100);
	if(pcmdev == NULL) {
		fprintf(stderr, "Error opening audio device %d\n", errno);
		fprintf(stderr, "Please check your libao settings and try again\n");
		return -1;
	}
	snd_pcm_drain(pcmdev);
	snd_pcm_close(pcmdev);

	memset(&raop_cbs, 0, sizeof(raop_cbs));
	raop_cbs.audio_init = audio_init;
	raop_cbs.audio_process = audio_process;
	raop_cbs.audio_destroy = audio_destroy;
	raop_cbs.audio_set_volume = audio_set_volume;
	raop_cbs.audio_set_progress = audio_set_progress;

	raop = raop_init_from_keyfile(10, &raop_cbs, "airport.key", NULL);
	if(raop == NULL) {
		fprintf(stderr, "Could not initialize the RAOP service\n");
		fprintf(stderr, "Please make sure the airport.key file is in the current directory.\n");
		return -1;
	}


	// last minute choices
	unsigned short port = 5000;
	char hwaddr[] = { 0x48, 0x5d, 0x60, 0x7c, 0xee, 0x22 };
	char *apname = "barry";
	char *password = NULL;
	raop_set_log_level(raop, RAOP_LOG_DEBUG);
	raop_start(raop, &port, hwaddr, sizeof(hwaddr), password);

	error = 0;
	dnssd = dnssd_init(&error);
	if (error) {
		fprintf(stderr, "ERROR: Could not initialize dnssd library!\n");
		fprintf(stderr, "------------------------------------------\n");
		fprintf(stderr, "You could try the following resolutions based on your OS:\n");
		fprintf(stderr, "Windows: Try installing http://support.apple.com/kb/DL999\n");
		fprintf(stderr, "Debian/Ubuntu: Try installing libavahi-compat-libdnssd-dev package\n");
		raop_destroy(raop);
		return -1;
	}

	dnssd_register_raop(dnssd, apname, port, hwaddr, sizeof(hwaddr), 0);

	running = 1;
	while(running)
		pause();

	dnssd_unregister_raop(dnssd);
	dnssd_destroy(dnssd);

	raop_stop(raop);
	raop_destroy(raop);

	return 0;
}
