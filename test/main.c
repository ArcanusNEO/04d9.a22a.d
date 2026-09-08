/* Offline transport simulation. This executable never opens a device. */
#define ioctl mock_ioctl
#define write mock_write
#define read mock_read
#define poll mock_poll
#define main utility_main
#include "../src/main.c"
#undef main
#undef poll
#undef read
#undef write
#undef ioctl
#include <stdarg.h>

static unsigned command_seen, remaining, writes, reads;
static bool wrong_reply, short_output, short_input, timeout_input, stale_input;
static uint8_t contents[BLOCK];

int
mock_ioctl (int fd, unsigned long request, ...)
{
  (void)fd;
  va_list args;
  va_start (args, request);
  uint8_t *p = va_arg (args, uint8_t *);
  va_end (args);
  if (request == HIDIOCSFEATURE (9))
    {
      unsigned sum = 0;
      for (unsigned i = 1; i < 9; i++)
        sum += p[i];
      if (p[0] || (sum & 255) != 255 || p[2] != 1)
        return -1;
      if ((p[1] == 0x0c && p[3] != BLOCK) || (p[1] == 0x8c && p[3] != 0))
        return -1;
      command_seen = p[1];
      remaining = BLOCK;
      return 9;
    }
  if (request == HIDIOCGFEATURE (9))
    {
      memset (p, 0, 9);
      p[1] = wrong_reply ? 0xff : (uint8_t)command_seen;
      p[2] = 1;
      p[3] = (uint8_t)remaining;
      return 9;
    }
  return -1;
}

ssize_t
mock_write (int fd, const void *buffer, size_t size)
{
  (void)fd;
  const uint8_t *p = buffer;
  if (command_seen != 0x0c || remaining < 64 || size != 65 || p[0])
    return -1;
  writes++;
  if (short_output)
    return 64;
  memcpy (contents + BLOCK - remaining, p + 1, 64);
  remaining -= 64;
  return 65;
}

ssize_t
mock_read (int fd, void *buffer, size_t size)
{
  (void)fd;
  if (command_seen != 0x8c || remaining < 64 || size != 65)
    return -1;
  memcpy (buffer, contents + BLOCK - remaining, 64);
  remaining -= 64;
  reads++;
  return short_input ? 63 : 64;
}

int
mock_poll (struct pollfd *fds, nfds_t count, int timeout)
{
  if (count != 1)
    return -1;
  if (!timeout)
    return stale_input ? 1 : 0;
  fds[0].revents = POLLIN;
  return timeout_input ? 0 : 1;
}

static void
reset_mock (void)
{
  command_seen = remaining = writes = reads = 0;
  wrong_reply = short_output = short_input = timeout_input = stale_input
      = false;
}

static void
test_pure_logic (void)
{
  uint8_t data[BLOCK];
  memset (data, 0, BLOCK);

  /* rate mapping */
  if (rate_to_raw (1000) != 0x01 || rate_to_raw (500) != 0x02
      || rate_to_raw (250) != 0x04 || rate_to_raw (125) != 0x08
      || rate_to_raw (999) != 0 || rate_to_raw (0) != 0)
    fail ("FAIL: rate_to_raw");
  if (raw_to_rate_hz (0x01) != 1000 || raw_to_rate_hz (0x02) != 500
      || raw_to_rate_hz (0x04) != 250 || raw_to_rate_hz (0x08) != 125
      || raw_to_rate_hz (0x03) != 0 || raw_to_rate_hz (0x00) != 0)
    fail ("FAIL: raw_to_rate_hz");

  /* scroll direction */
  for (unsigned i = 0; i < BLOCK; i++)
    data[i] = 0;
  data[SCROLL_REC14] = SCROLL_TYPE;
  data[SCROLL_REC14 + 2] = SCROLL_UP;
  data[SCROLL_REC15] = SCROLL_TYPE;
  data[SCROLL_REC15 + 2] = SCROLL_DOWN;
  if (!scroll_is_valid (data) || scroll_is_natural (data))
    fail ("FAIL: scroll normal state");
  scroll_set_direction (data, true);
  if (!scroll_is_valid (data) || !scroll_is_natural (data))
    fail ("FAIL: scroll natural state");
  if (data[SCROLL_REC14 + 2] != SCROLL_DOWN
      || data[SCROLL_REC15 + 2] != SCROLL_UP)
    fail ("FAIL: scroll natural bytes");
  scroll_set_direction (data, false);
  if (scroll_is_natural (data))
    fail ("FAIL: scroll back to normal");

  /* raw_dpi 6-bit wrap */
  data[84] = 16;
  if (raw_dpi (data, 1, false) != 16)
    fail ("FAIL: raw_dpi slot1");
  data[84] = 64;
  if (raw_dpi (data, 1, false) != 0)
    fail ("FAIL: raw_dpi wrap 64");
  data[84] = 80;
  if (raw_dpi (data, 1, false) != 16)
    fail ("FAIL: raw_dpi wrap 80");
  data[84] = 255;
  if (raw_dpi (data, 1, false) != 63)
    fail ("FAIL: raw_dpi wrap 255");

  /* edit_dpi */
  edit_dpi (data, 1, 3200);
  if (data[84] != 32)
    fail ("FAIL: edit_dpi");

  puts ("PASS: pure logic (rate map, scroll direction, 6-bit DPI wrap).");
}

int
main (void)
{
  uint8_t input[BLOCK], output[BLOCK];
  if (self_test ())
    return 1;
  test_pure_logic ();
  for (unsigned i = 0; i < BLOCK; i++)
    input[i] = (uint8_t)(i * 13);
  reset_mock ();
  if (write_profile (-1, 1, input) || remaining || writes != 2
      || memcmp (contents, input, BLOCK))
    fail ("FAIL: synchronized two-chunk write");
  reset_mock ();
  if (read_profile (-1, 1, output) || reads != 2
      || memcmp (output, input, BLOCK))
    fail ("FAIL: two-chunk read");
  reset_mock ();
  wrong_reply = true;
  if (write_profile (-1, 1, input) != -1 || writes)
    fail ("FAIL: invalid readiness response must prevent output");
  reset_mock ();
  short_output = true;
  if (write_profile (-1, 1, input) != -1 || writes != 1)
    fail ("FAIL: partial write must stop");
  reset_mock ();
  wrong_reply = true;
  if (read_profile (-1, 1, output) != -1 || reads)
    fail ("FAIL: invalid read acknowledgement");
  reset_mock ();
  timeout_input = true;
  if (read_profile (-1, 1, output) != -1 || reads)
    fail ("FAIL: read timeout");
  reset_mock ();
  short_input = true;
  if (read_profile (-1, 1, output) != -1 || reads != 1)
    fail ("FAIL: partial input must stop");
  reset_mock ();
  stale_input = true;
  if (read_profile (-1, 1, output) != -1 || command_seen)
    fail ("FAIL: reject stale reports before querying");
  puts ("PASS: mocked transport, readiness checks, partial transfers, "
        "timeouts, stale data.");
  return 0;
}
