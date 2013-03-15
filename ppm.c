#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>
#include <linux/input.h>
#include <linux/uinput.h>

bool debug = true;

typedef struct {
  unsigned int rate; // soundcard sampling rate in Hz
  size_t sync_min, sync_max; // allowed length of sync pulse in samples
  int16_t threshhold; // threshhold to be a high pulse
} pulse_params_t;

typedef struct {
  enum { INIT, LOW, HIGH, SYNC, INVALID } type; // type of pulse
  size_t length; // length of pulse in samples
} pulse_t;

typedef struct {
  unsigned int period; // length of a full cycle in samples
  size_t sync_min; // minimum length of sync pulse in samples
  size_t sync_max; // maximum length of sync pulse in samples
  size_t low_min; // minimum length of low pulse in samples
  size_t low_max; // maximum length of low pulse in samples
  size_t high_min; // minimum length of high pulse in samples
  size_t high_max; // maximum length of high pulse in samples
} tx_parms;

typedef struct {
  pulse_params_t params;
  size_t samples;
  pulse_t pulse;
  int16_t (*buffer)[2];
  size_t offset;
  snd_pcm_t *handle;
} state_t;

void init_pulse(pulse_t *p)
{
  if (p) {
    p->type = INIT;
    p->length = 0;
  }
}

void destroy_alsa (state_t *state)
{
  if (state->buffer)
    free(state->buffer);

  if (state->handle)
    snd_pcm_close(state->handle);
}

int init_alsa (state_t *state, char *dev, unsigned int rate, unsigned int period, unsigned int sync_length, int16_t threshhold)
{
  assert(state);
  int ret = 0;
  int err;
  snd_pcm_hw_params_t *hw_params = 0;

  if ((err = snd_pcm_open (&state->handle, dev, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    fprintf (stderr, "cannot open audio device %s (%s)\n",
       dev,
       snd_strerror (err));
    goto error;
  }

  if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
    fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
       snd_strerror (err));
    goto error;
  }

  if ((err = snd_pcm_hw_params_any (state->handle, hw_params)) < 0) {
    fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
       snd_strerror (err));
    goto error;
  }

  if ((err = snd_pcm_hw_params_set_access (state->handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    fprintf (stderr, "cannot set access type (%s)\n",
       snd_strerror (err));
    goto error;
  }

  if ((err = snd_pcm_hw_params_set_format (state->handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
    fprintf (stderr, "cannot set sample format (%s)\n",
       snd_strerror (err));
    goto error;
  }

  if ((err = snd_pcm_hw_params_set_rate_near (state->handle, hw_params, &rate, 0)) < 0) {
    fprintf (stderr, "cannot set sample rate (%s)\n",
       snd_strerror (err));
    goto error;
  }

  if ((err = snd_pcm_hw_params_set_channels (state->handle, hw_params, 2)) < 0) {
    fprintf (stderr, "cannot set channel count (%s)\n",
       snd_strerror (err));
    goto error;
  }

  if ((err = snd_pcm_hw_params (state->handle, hw_params)) < 0) {
    fprintf (stderr, "cannot set parameters (%s)\n",
       snd_strerror (err));
    goto error;
  }

  if ((err = snd_pcm_prepare (state->handle)) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
       snd_strerror (err));
    goto error;
  }

  state->samples = (rate*period + 999)/1000;
  state->params.rate = rate;
  state->params.sync_min = (sync_length*rate + 999)/1000;
  state->params.sync_max = 2*state->samples;
  state->params.threshhold = threshhold;
  state->buffer = malloc(state->samples*sizeof(int16_t[2])); // one period worth of buffer
  state->offset = state->samples; // indicate that buffer contains no data
  init_pulse(&state->pulse);

  goto cleanup;

  error:
    ret = 1;
    destroy_alsa(state);

  cleanup:
    snd_pcm_hw_params_free (hw_params);
    return ret;
}

// return true if datum starts a new pulse, false otherwise
bool datum_to_pulse(int16_t datum, pulse_params_t *params, pulse_t *p)
{
  bool complete = false;
  if (datum > params->threshhold) { // high signal
    switch (p->type) {
      case INIT: // start of high pulse
        p->type = HIGH;
        p->length = 1;
        break;
      case LOW: // end of high pulse
        complete = true;
        break;
      case HIGH: // continue high pulse
        p->length++;
        break;
      default: // error
        exit(1);
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
        complete = true;
        break;
      default: // error
        exit(1);
    }
  }

  if (complete) {
    if (p->length >= params->sync_min) {
      if (p->type == LOW && p->length <= params->sync_max)
        p->type = SYNC; // this is really a sync pulse
      else
        p->type = INVALID; // pulse is too long
    }

    if (debug) {
      switch (p->type) {
        case LOW:
          printf("L");
          break;
        case HIGH:
          printf("H");
          break;
        case SYNC:
          printf("S");
          break;
        default:
          printf("I");
        }
      printf("%zu ", p->length);
    }
  }

  return complete;
}

bool data_to_pulse(int16_t (*data)[2], size_t *offset, size_t samples, pulse_params_t *params, pulse_t *p)
{
  while (*offset < samples) {
    if (datum_to_pulse(data[*offset][0], params, p))
      return true; // datum starts a new pulse, so don't update offset
    else
      (*offset)++;
  }
  return false;
}

void read_pulse_alsa(state_t *state)
{
  int err;
  init_pulse(&state->pulse);
  for (;;) {
    if (state->offset == state->samples) { // refill buffer
      if ((err = snd_pcm_readi (state->handle, state->buffer, state->samples)) != state->samples) {
        fprintf (stderr, "read from audio interface failed (%s)\n",
           snd_strerror (err));
        exit (1);
      }
      state->offset = 0;
    }
    if (data_to_pulse(state->buffer, &state->offset, state->samples, &state->params, &state->pulse))
      break; // found a complete pulse
  }
}

int main (int argc, char *argv[])
{
  int i;
  int err;

  /* initialize alsa stuff */
  state_t state;
  init_alsa(&state, "hw:0", 1000000, 10, 5, 32700);

  /* initialize uinput joystick stuff */
  int uinput;
  if (!debug) {
    uinput = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput < 0) {
      fprintf(stderr, "/dev/input/uinput: %s\n", strerror(errno));
      exit(1);
    }

    /* we assign channels 0-6 to X, Y, Z, RX, RY, RZ, and simulate a button with the gyro channel */
    err = ioctl(uinput, UI_SET_EVBIT, EV_ABS);
    err = ioctl(uinput, UI_SET_ABSBIT, ABS_X);
    err = ioctl(uinput, UI_SET_ABSBIT, ABS_Y);
    err = ioctl(uinput, UI_SET_ABSBIT, ABS_Z);
    err = ioctl(uinput, UI_SET_ABSBIT, ABS_RX);
    err = ioctl(uinput, UI_SET_ABSBIT, ABS_RY);
    err = ioctl(uinput, UI_SET_ABSBIT, ABS_RZ);
    err = ioctl(uinput, UI_SET_EVBIT, EV_KEY);
    err = ioctl(uinput, UI_SET_KEYBIT, BTN_JOYSTICK);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "ppmjoy");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x1234;
    uidev.id.product = 0xfedc;
    uidev.id.version = 1;
    for (i = 0; i < 6; i++) {
      uidev.absmax[i] = (2500*state.params.rate)/1000000;
      // set maximum values to a pulse length of 2.5ms
    }
    err = write(uinput, &uidev, sizeof(uidev));
    err = ioctl(uinput, UI_DEV_CREATE);
  }

  /* read pulses from TX and forward them to uinput */

  init: // look for a sync pulse
  if (debug)
    printf("init\n");

  for (;;) {
    read_pulse_alsa(&state);
    if (state.pulse.type == SYNC)
      break; // found a complete sync pulse
  }

  // ignore initial high pulse
  read_pulse_alsa(&state);
  if (state.pulse.type != HIGH)
    goto init;

  if (debug)
    printf("\n");

  int prev_length[6] = {0, 0, 0, 0, 0, 0};
  for (;;) {
    for (i = 0; i < 6; i++) {
      struct input_event ev;
      memset(&ev, 0, sizeof(ev));
      ev.type = EV_ABS;
      ev.code = ABS_X+i;

      // look for a low pulse
      read_pulse_alsa(&state);
      if (state.pulse.type != LOW)
        goto init;
      ev.value = state.pulse.length;

      // followed by a high pulse
      read_pulse_alsa(&state);
      if (state.pulse.type != HIGH)
        goto init;
      ev.value += state.pulse.length;

      int diff = ev.value - prev_length[i];
      // remove jitter on rudder channel
      if (i == 3 && diff >= -1 && diff <= 1)
        ev.value = prev_length[i];

      prev_length[i] = ev.value;

      if (debug) {
        printf("%i ", ev.value);
      } else {
        // send value to uinput
        err = write(uinput, &ev, sizeof(ev));
      }

      // check if the gyro switch was moved
      // gyro channel seems especially jittery
/*      if (i == 4 && (diff > 3 || diff < -3)) {
        ev.type = EV_KEY;
        ev.code = BTN_JOYSTICK;
        // depress button if channel increased, release if decreased
        if (diff > 3)
          ev.value = 1;
        else
          ev.value = 0;
        err = write(uinput, &ev, sizeof(ev));
      } */
    }

    // skip sync pulse and following high pulse
    read_pulse_alsa(&state);
    if (state.pulse.type != SYNC)
      goto init;

    read_pulse_alsa(&state);
    if (state.pulse.type != HIGH)
      goto init;

    if (debug)
      printf("\n");
  }

  destroy_alsa(&state);
  ioctl(uinput, UI_DEV_DESTROY);
  close(uinput);
  exit (0);
}
