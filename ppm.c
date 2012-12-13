#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
      
unsigned int rate = 192000; // soundcard sample rate in Hz
const unsigned int sync_length = 5; // minimum length of sync pulse in ms
const short threshhold = 15000; // threshhold for a high signal

int read_pulse(short *buf, size_t *offset, size_t buf_len, unsigned int *pulse_len)
{
  buf += 2*(*offset);
  buf_len -= *offset;
  int i = 1;
  int type;
  if (buf[0] < threshhold) { // start of low pulse
    type = 0;
    for (; i < buf_len; i++)
      if (buf[2*i] >= threshhold) // end of low pulse
        break;
  } else { // start of high pulse
    type = 1;
    for (; i < buf_len; i++)
      if (buf[2*i] < threshhold) // end of high pulse
        break;
  }
  *offset += i;
  *pulse_len = i;
  return type;
}
      
main (int argc, char *argv[])
{
  int samples;
	int err;
	snd_pcm_t *capture_handle;
	snd_pcm_hw_params_t *hw_params;

	if ((err = snd_pcm_open (&capture_handle, argv[1], SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		fprintf (stderr, "cannot open audio device %s (%s)\n", 
			 argv[1],
			 snd_strerror (err));
		exit (1);
	}
	   
	if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
		fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
			 snd_strerror (err));
		exit (1);
	}
			 
	if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
		fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
			 snd_strerror (err));
		exit (1);
	}

	if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		fprintf (stderr, "cannot set access type (%s)\n",
			 snd_strerror (err));
		exit (1);
	}

	if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
		fprintf (stderr, "cannot set sample format (%s)\n",
			 snd_strerror (err));
		exit (1);
	}

	if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &rate, 0)) < 0) {
		fprintf (stderr, "cannot set sample rate (%s)\n",
			 snd_strerror (err));
		exit (1);
	}
  samples = (40*rate + 999)/1000; // make sure at least 40ms is captured
  printf("%i\n", samples);

	if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, 2)) < 0) {
		fprintf (stderr, "cannot set channel count (%s)\n",
			 snd_strerror (err));
		exit (1);
	}

	if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
		fprintf (stderr, "cannot set parameters (%s)\n",
			 snd_strerror (err));
		exit (1);
	}

	snd_pcm_hw_params_free (hw_params);

	if ((err = snd_pcm_prepare (capture_handle)) < 0) {
		fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
			 snd_strerror (err));
		exit (1);
	}

  size_t buf_len = 2*samples*sizeof(short);
  short *buf = malloc(buf_len);
	for (;;) {
		if ((err = snd_pcm_readi (capture_handle, buf, samples)) != samples) {
			fprintf (stderr, "read from audio interface failed (%s)\n",
				 snd_strerror (err));
			exit (1);
		}

    int sync_length_cycles = (sync_length*rate + 999)/1000;
    size_t offset = 0;
    unsigned int low_len, high_len;

    // first discard everything until the end of the sync pulse
    while (read_pulse(buf, &offset, buf_len, &low_len) || low_len < sync_length_cycles);

    int i = 0;
    for (; i < 6; i++) {
      read_pulse(buf, &offset, buf_len, &high_len);
      read_pulse(buf, &offset, buf_len, &low_len);
      printf("%u ", high_len + low_len);
    }
    printf("\n");
	}

	snd_pcm_close (capture_handle);
	exit (0);
}
