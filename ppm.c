#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
      
main (int argc, char *argv[])
{
	unsigned int rate = 192000; // soundcard sample rate in Hz
  unsigned int sync_length = 5; // minimum length of sync pulse in ms
  short threshhold = 15000; // threshhold for a high signal
  int samples;
	int err;
  int i;
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

  short *buf = malloc(2*samples*sizeof(short));
	for (;;) {
		if ((err = snd_pcm_readi (capture_handle, buf, samples)) != samples) {
			fprintf (stderr, "read from audio interface failed (%s)\n",
				 snd_strerror (err));
			exit (1);
		}

    int high = 0, low = 0, max = -32768, min = 32767;
    int sync_length_cycles = (sync_length*rate + 999)/1000;

    // first discard everything until the end of the sync pulse
    for (i = 0; i < samples; i++) {
      if (buf[2*i] <= threshhold)
        low++;
      else if (low >= sync_length_cycles)
        break;
      else
        low = 0;
    }

    low = 0;
    for (; i < samples; i++) {
//      printf("%hi ", buf[2*i]);
      if (max < buf[2*i]) max = buf[2*i];
      if (min > buf[2*i]) min = buf[2*i];
      if (buf[2*i] > threshhold) {
        high++;
        if (low > 0) { // end of low signal
          if (low >= sync_length_cycles) { // end of cycle, skip the rest
            low = 0;
            break;
          }
          printf("L%i ", low);
          low = 0;
        }
      } else {
        low++;
        if (high > 0) { // end of high signal
          printf("H%i ", high);
          high = 0;
        }
      }
    }
    if (low > 0) // end of low signal
      printf("L%i ", low);
    if (high > 0) // end of high signal
      printf("H%i ", high);
    printf("\n");
//    printf("%hd %hd\n", min, max);
	}

	snd_pcm_close (capture_handle);
	exit (0);
}
