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

#define nelem(x) sizeof(x)/sizeof(x[0])

typedef struct ShairSession ShairSession;
struct ShairSession {
	snd_pcm_t *pcmdev;
};


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

	// nonblocking mode.. but we also don't want to buffer much. so we should
	// slightly speed up or slow down to stay in the middle. to be implemented.
	snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);

	snd_pcm_hw_params_t *hw_params;
	snd_pcm_hw_params_malloc(&hw_params);
	snd_pcm_hw_params_any(pcm, hw_params);
	snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_channels(pcm, hw_params, 2);
	snd_pcm_hw_params_set_rate(pcm, hw_params, 44100, 0);
	snd_pcm_hw_params_set_periods(pcm, hw_params, 13, 0);
	snd_pcm_hw_params_set_period_time(pcm, hw_params, 8*1000, 0);
	snd_pcm_hw_params_set_period_size(pcm, hw_params, 352, 0);
	snd_pcm_hw_params(pcm, hw_params);
	snd_pcm_hw_params_free(hw_params);

	return pcm;
}

static void *
audio_init(void *cls, int bits, int channels, int samplerate, int *audio_fd)
{
	ShairSession *sp = malloc(sizeof sp[0]);
	memset(sp, 0, sizeof sp[0]);

	sp->pcmdev = audio_open_device(bits, channels, samplerate);
	if (sp->pcmdev == NULL) {
		printf("Error opening device %d\n", errno);
		printf("The device might already be in use");
	}

	// the rtp thread uses select, so we dig into the poll descriptors,
	// find the output fd and return it. also spit out as much info as possible

	struct pollfd pollfds[8];
	int npoll = snd_pcm_poll_descriptors(sp->pcmdev, pollfds, nelem(pollfds));

	// just for debug purposes.
	static int pollflags[] = {
		POLLIN,
		POLLPRI,
		POLLOUT,
		POLLERR,
		POLLHUP,
		POLLNVAL,
	};

	static char *pollnames[] = {
		"POLLIN",
		"POLLPRI",
		"POLLOUT",
		"POLLERR",
		"POLLHUP",
		"POLLNVAL"
	};

	for(int i = 0; i < npoll; i++){
		fprintf(stderr, "audio_init: pollfds[%d]: fd %d", i, pollfds[i].fd);
		for(int fi = 0; fi < nelem(pollflags); fi++)
			if(pollfds[i].events & pollflags[fi])
				fprintf(stderr, " %s", pollnames[fi]);
		if(pollfds[i].events & POLLOUT){
			fprintf(stderr, " - passing this to rtp thread");
			*audio_fd = pollfds[i].fd;
		}
		fprintf(stderr, "\n");
	}

	return sp;
}

static void
audio_process(void *cls, void *opaque, const void *abuf, int len, unsigned int timestamp, unsigned int ltime)
{
	ShairSession *sp = opaque;
	const char *buf = abuf;

	struct {
		short left;
		short right;
	} frames[1024];

	while(len > 0){
		int nbytes = (len < sizeof frames) ? len : sizeof frames;
		memcpy(frames, buf, nbytes);
		int nframes = nbytes / sizeof frames[0];
		for(int i = 0; i < nframes; i++){
			int sum = (frames[i].left + frames[i].right) / 2;
			frames[i].left = sum;
			frames[i].right = sum;
		}
		if(sp->pcmdev != NULL){
			int nwr = snd_pcm_writei(sp->pcmdev, frames, nframes);
			if(nwr > 0 && nwr < nframes){
				fprintf(stderr, "pcmdev short write: buffers full\n");
			} else if(nwr == -EAGAIN){
				fprintf(stderr, "pcmdev eagain: buffers full\n");
			} else if(nwr == -EPIPE || nwr == -EINTR || nwr == -ESTRPIPE ){
				snd_pcm_recover(sp->pcmdev, nwr, 0);
				fprintf(stderr, "pcmdev broken pipe nframes %d\n", nframes);
			}

			// we just wrote nframes after timestamp.
			// the sample emanating from the speaker is (timestamp+nframes)-snd_pcm_delay.
			// ... in theory.
			long delay;
			snd_pcm_delay(sp->pcmdev, &delay);
			fprintf(stderr, "pcmdev delay %ld buffered %u\n", delay, ltime-timestamp+delay);
		}
		len -= nbytes;
	}
}

static void
audio_destroy(void *cls, void *opaque)
{
	ShairSession *sp = opaque;

	if(sp->pcmdev != NULL){
		snd_pcm_drain(sp->pcmdev);
		snd_pcm_close(sp->pcmdev);
	}
	free(sp);
}

static void
audio_set_volume(void *cls, void *opaque, float volume)
{
	// can't do much here until I figure the mixer out.
	//ShairSession *sp = opaque;
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

	// run until termination signal
	running = 1;
	while(running)
		pause();

	dnssd_unregister_raop(dnssd);
	dnssd_destroy(dnssd);

	raop_stop(raop);
	raop_destroy(raop);

	return 0;
}
