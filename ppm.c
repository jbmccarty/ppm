#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

unsigned int rate = 192000; // soundcard sample rate in Hz
const unsigned int sync_length = 5; // minimum length of sync pulse in ms
int16_t threshhold = 32700; // threshhold for a high signal

typedef struct {
  enum { INIT, LOW, HIGH } type; // type of pulse
  size_t length; // length of pulse in samples
} pulse;

typedef struct {
  unsigned int period; // length of a full cycle in samples
  size_t sync_min; // minimum length of sync pulse in samples
  size_t sync_max; // maximum length of sync pulse in samples
  size_t low_min; // minimum length of low pulse in samples
  size_t low_max; // maximum length of low pulse in samples
  size_t high_min; // minimum length of high pulse in samples
  size_t high_max; // maximum length of high pulse in samples
} tx_parms;


void init_pulse(pulse *p)
{
  if (p) {
    p->type = INIT;
    p->length = 0;
  }
}

// return true if datum starts a new pulse, false otherwise
bool datum_to_pulse(int16_t datum, int16_t threshhold, pulse *p)
{
  bool complete = 0;
  if (datum > threshhold) { // high signal
    switch (p->type) {
      case INIT: // start of high pulse
        p->type = HIGH;
        p->length = 1;
        break;
      case LOW: // end of high pulse
        complete = 1;
        break;
      case HIGH: // continue high pulse
        p->length++;
        break;
    }
  } else { // low signal
    switch (p->type) {
      case INIT: // start of low pulse
        p->type = LOW;
        p->length = 1;
        break;
      case LOW: // continue low pulse
        p->length++;
        break;
      case HIGH: // end of low pulse
        complete = 1;
        break;
    }
  }

  return complete;
}

bool data_to_pulse(int16_t (*data)[2], size_t *offset, size_t samples, int16_t threshhold, pulse *p)
{
  while (*offset < samples) {
    if (datum_to_pulse(data[*offset][0], threshhold, p))
      return true; // datum starts a new pulse, so don't update offset
    else
      (*offset)++;
  }
  return false;
}

void alsa_to_pulse(snd_pcm_t *capture_handle, int16_t (*data)[2], size_t *offset, size_t samples, int16_t threshhold, pulse *p)
{
  int err;
  init_pulse(p);
  for (;;) {
    if (*offset == samples) { // refill buffer
      if ((err = snd_pcm_readi (capture_handle, data, samples)) != samples) {
        fprintf (stderr, "read from audio interface failed (%s)\n",
           snd_strerror (err));
        exit (1);
      }
      *offset = 0;
    }
    if (data_to_pulse(data, offset, samples, threshhold, p))
      break; // found a complete pulse
  }
}

main (int argc, char *argv[])
{
  unsigned int rate = 1000000; // audio sample rate in Hz; will be updated by ALSA call
  unsigned int period = 20; // length of TX cycle in ms
  size_t samples; // number of samples in a period
  enum { ST_UNKNOWN, ST_SYNC, ST_LOW, ST_HIGH } state = ST_UNKNOWN; // type of current pulse
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

  samples = (rate*period + 999)/2000; // update every half a TX period
  int sync_length_cycles = (sync_length*rate + 999)/1000;
  int16_t (*data)[2] = malloc(samples*sizeof(int16_t[2])); // one period worth of buffer
  size_t offset;
  pulse p;

  init: // look for a sync pulse
  offset = samples;
  for (;;) {
    alsa_to_pulse(capture_handle, data, &offset, samples, threshhold, &p);
    if (p.type == LOW && p.length >= sync_length_cycles)
      break; // found a complete sync pulse
  }

  for (;;) {
    int i;
    for (i = 0; i < 6; i++) {
      size_t l;
      alsa_to_pulse(capture_handle, data, &offset, samples, threshhold, &p);
      l = p.length;
      alsa_to_pulse(capture_handle, data, &offset, samples, threshhold, &p);
      l += p.length;
//      printf("H%zu ", l);
    }
//    printf("\n");
    alsa_to_pulse(capture_handle, data, &offset, samples, threshhold, &p);
    if (p.length < sync_length_cycles || p.length > samples)
      goto init;
  }

  snd_pcm_close (capture_handle);
  exit (0);
}
