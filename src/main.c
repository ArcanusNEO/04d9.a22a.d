#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* Experimental API B support for the inspected 04d9:a22a revision 0101.
 * The OEM Windows package identifies Sensor=3327, conflicting with an earlier
 * PAW3333 inference; the physical sensor is unresolved. Tested 1200/1600/3200
 * writes use the direct low-range 100-CPI mapping. The OEM 3327 writer uses a
 * bit-6 high-range encoding from n >= 63; the CLI now mirrors that encoder and
 * displays an inverse CPI estimate. Physical calibration remains unverified.
 * This CLI writes X only and preserves Y; profile X/Y scale effects are unknown.
 */
enum
{
  BLOCK = 128,
  DPI_CODE_MASK = 0x3f,
  DPI_EXTENDED = 0x40,
  DPI_RESERVED = 0x80,
  MIN_DPI = 200,
  MAX_DPI = 12400,
  PROFILES = 6
};
/* Snapshot format: magic(9) version(2 LE) then, for each profile 0..5, the
 * 128-byte config block followed by the 128-byte button block, then a 4-byte
 * little-endian FNV-1a32 checksum over all preceding bytes. */
enum
{
  SNAP_MAGIC_LEN = 9,
  SNAP_SIZE = SNAP_MAGIC_LEN + 2 + PROFILES * 2 * BLOCK + 4
};
static const uint8_t descriptor[]
    = { 0x06, 0x00, 0xff, 0x0a, 0x00, 0xff, 0xa1, 0x01, 0x15, 0x00, 0x26,
        0xff, 0x00, 0x09, 0x20, 0x75, 0x08, 0x95, 0x40, 0x81, 0x02, 0x09,
        0x21, 0x91, 0x02, 0x09, 0x22, 0x95, 0x08, 0xb1, 0x02, 0xc0 };

static void
fail (const char *message)
{
  fprintf (stderr, "%s\n", message);
  exit (EXIT_FAILURE);
}

static void
packet (uint8_t out[9], uint8_t command, uint8_t a, uint8_t b)
{
  memset (out, 0, 9);
  out[1] = command;
  out[2] = a;
  out[3] = b;
  out[8] = (uint8_t)(255 - command - a - b);
}

static int
send_command (int fd, uint8_t command, uint8_t a, uint8_t b)
{
  uint8_t out[9];
  packet (out, command, a, b);
  return ioctl (fd, HIDIOCSFEATURE (sizeof (out)), out) == 9 ? 0 : -1;
}

static int
get_feature (int fd, uint8_t reply[9])
{
  memset (reply, 0, 9);
  return ioctl (fd, HIDIOCGFEATURE (9), reply) == 9 ? 0 : -1;
}

static int
query (int fd, uint8_t command, uint8_t profile, uint8_t reply[9])
{
  if (send_command (fd, command, profile, 0) || get_feature (fd, reply))
    return -1;
  return reply[0] == 0 && reply[1] == command ? 0 : -1;
}

static int
get_active_profile (int fd, uint8_t *profile)
{
  uint8_t reply[9];
  if (query (fd, 0x82, 0, reply) || reply[2] >= PROFILES)
    return -1;
  *profile = reply[2];
  return 0;
}

static int
current (int fd, uint8_t *profile, uint8_t *slot)
{
  uint8_t reply[9];
  if (get_active_profile (fd, profile))
    return -1;
  if (query (fd, 0x84, *profile, reply) || reply[2] != *profile || reply[3] < 1
      || reply[3] > 8)
    return -1;
  *slot = reply[3];
  return 0;
}

static int
set_active_profile (int fd, uint8_t profile)
{
  uint8_t check;
  if (send_command (fd, 0x02, profile, 0))
    return -1;
  if (get_active_profile (fd, &check) || check != profile)
    return -1;
  return 0;
}

static const unsigned rate_hz[] = { 0, 1000, 500, 0, 250, 0, 0, 0, 125 };

static unsigned
raw_to_rate_hz (uint8_t raw)
{
  if (raw < sizeof (rate_hz) / sizeof (rate_hz[0]))
    return rate_hz[raw];
  return 0;
}

static uint8_t
rate_to_raw (unsigned hz)
{
  switch (hz)
    {
    case 1000:
      return 0x01;
    case 500:
      return 0x02;
    case 250:
      return 0x04;
    case 125:
      return 0x08;
    default:
      return 0;
    }
}

/* Query the active report rate code for a profile. Returns the raw code or 0.
 */
static int
get_rate (int fd, uint8_t profile, uint8_t *raw)
{
  uint8_t reply[9];
  if (query (fd, 0x83, profile, reply) || reply[2] != profile)
    return -1;
  *raw = reply[3];
  return 0;
}

/* Read a 128-byte block. cmd is the read command (0x8c profile, 0x8d buttons).
 */
static int
read_block (int fd, uint8_t cmd, uint8_t profile, uint8_t data[BLOCK])
{
  uint8_t reply[9], chunk[65];
  struct pollfd p = { .fd = fd, .events = POLLIN };
  /* Do not discard unexplained reports and risk interpreting stale data. */
  if (poll (&p, 1, 0) != 0 || query (fd, cmd, profile, reply)
      || reply[2] != profile || reply[3] != BLOCK)
    return -1;
  for (int i = 0; i < 2; i++)
    {
      if (poll (&p, 1, 1500) != 1 || !(p.revents & POLLIN)
          || (p.revents & (POLLERR | POLLHUP | POLLNVAL))
          || read (fd, chunk, sizeof (chunk)) != 64)
        return -1;
      memcpy (data + i * 64, chunk, 64);
    }
  return 0;
}

static int
read_profile (int fd, uint8_t profile, uint8_t data[BLOCK])
{
  return read_block (fd, 0x8c, profile, data);
}

static int
read_buttons (int fd, uint8_t profile, uint8_t data[BLOCK])
{
  return read_block (fd, 0x8d, profile, data);
}

static int
ready (int fd, uint8_t cmd, uint8_t profile, uint8_t remaining)
{
  uint8_t reply[9];
  const struct timespec delay = { .tv_nsec = 1000000 };
  for (int i = 0; i < 50; i++)
    {
      if (get_feature (fd, reply) || reply[0] || reply[1] != cmd
          || reply[2] != profile)
        return -1;
      if (reply[3] == remaining)
        return 0;
      nanosleep (&delay, NULL);
    }
  return -1;
}

static int
write_block (int fd, uint8_t cmd, uint8_t profile, const uint8_t data[BLOCK])
{
  uint8_t out[65] = { 0 };
  if (send_command (fd, cmd, profile, BLOCK))
    return -1;
  /* The reference protocol warns that unsynchronized writes can soft-brick. */
  for (int i = 0; i < 2; i++)
    {
      if (ready (fd, cmd, profile, (uint8_t)(BLOCK - i * 64)))
        return -1;
      memcpy (out + 1, data + i * 64, 64);
      if (write (fd, out, sizeof (out)) != (ssize_t)sizeof (out))
        return -1;
    }
  return ready (fd, cmd, profile, 0);
}

static int
write_profile (int fd, uint8_t profile, const uint8_t data[BLOCK])
{
  return write_block (fd, 0x0c, profile, data);
}

static int
write_buttons (int fd, uint8_t profile, const uint8_t data[BLOCK])
{
  return write_block (fd, 0x0d, profile, data);
}

static uint8_t
raw_dpi (const uint8_t data[BLOCK], unsigned slot, bool y)
{
  return data[(y ? 92 : 84) + slot - 1];
}

/* Sensor=3327 write encoding recovered from the OEM Windows application's
 * profile writer. `dpi / 100` is kept in the sensor's direct range through
 * 6200; above that, bit 6 selects the half-rate/high-range representation. */
static uint8_t
encode_dpi (unsigned dpi)
{
  unsigned n = dpi / 100;
  if (n == 1)
    return 2;
  if (n < 63)
    return (uint8_t)n;
  return (uint8_t)((n / 2) | DPI_EXTENDED);
}

/* The inverse is an estimate: the OEM writer's odd high-range values collide
 * with the preceding even value, and no physical CPI calibration is available. */
static bool
decode_dpi (uint8_t raw, unsigned *dpi)
{
  if ((raw & DPI_RESERVED) || raw == 1 || raw == 0x3f)
    return false;
  unsigned code = raw & DPI_CODE_MASK;
  if (raw & DPI_EXTENDED)
    {
      if (code < 31 || raw > 0x7e)
        return false;
      *dpi = code * 200;
    }
  else
    *dpi = code * 100;
  return true;
}

static bool
estimate_slot_dpi (const uint8_t data[BLOCK], unsigned slot, unsigned *dpi)
{
  if (slot < 1 || slot > 8 || (data[82] & (1u << (slot - 1))))
    return false;
  return decode_dpi (raw_dpi (data, slot, false), dpi);
}

static void
edit_dpi (uint8_t data[BLOCK], unsigned slot, unsigned dpi)
{
  data[84 + slot - 1] = encode_dpi (dpi);
}

/* Scroll records occupy button-config bytes 56..63: record 14 (scroll up) and
 * record 15 (scroll down). Record layout: type(1) pad event pad. type 0x04 is
 * scroll; event 0x01 = up, 0x02 = down (reference decoding). */
enum
{
  SCROLL_REC14 = 56,
  SCROLL_REC15 = 60,
  SCROLL_TYPE = 0x04
};
enum
{
  SCROLL_UP = 0x01,
  SCROLL_DOWN = 0x02
};
/* Illumination fields in the 128-byte profile block. Offsets 71..73 and
 * 104..127 are per-profile; 16..47 are global and live only in profile 0.
 * The OEM writer's mode and parameter encodings are statically recovered;
 * physical effects are not yet hardware-verified. */
enum
{
  INDICATOR_ENABLE = 16, /* 8 bytes, profile 0 only */
  ILLUM_COLOR = 24,      /* 8 RGB triples (24..47), profile 0 only */
  ILLUM_MODE = 71,
  ILLUM_PARAM_A = 72,
  ILLUM_PARAM_B = 73,
  DPI_COLOR = 104 /* 8 RGB triples (104..127), per profile */
};

enum
{
  ILLUM_OFF = 0,
  ILLUM_STANDARD = 1,
  ILLUM_TWINKLE = 2,
  ILLUM_BREATHING = 3,
  ILLUM_NEON = 4,
  ILLUM_WAVE = 5,
  ILLUM_SLIDE = 6,
  ILLUM_FINGER = 7,
  ILLUM_FLIP = 8,
  ILLUM_YOYO = 9,
  ILLUM_CROSS = 10,
  ILLUM_FLYING = 11,
  ILLUM_MODE_COUNT = 12
};

static const char *const illum_mode_cli_names[ILLUM_MODE_COUNT]
    = { "off", "standard", "twinkle", "breathing", "neon", "wave",
        "slide", "finger", "flip", "yoyo", "cross", "flying" };
static const char *const illum_mode_labels[ILLUM_MODE_COUNT]
    = { "Off", "Standard", "Twinkle", "Breathing", "Neon", "Wave",
        "Slide", "Finger movement", "Flip up and down", "YO-YO",
        "Cross flash", "Flying star" };

static const char *illum_mode_name (uint8_t mode);
static int illum_mode_from_name (const char *name);
static int illum_mode_argument_count (unsigned mode);
static bool encode_illumination_parameters (unsigned mode, unsigned a,
                                           unsigned b, uint8_t *byte_a,
                                           uint8_t *byte_b);
static bool edit_illumination (uint8_t data[BLOCK], unsigned mode,
                               unsigned a, unsigned b);
static bool parse_rgb (const char *text, uint8_t rgb[3]);
static void edit_global_fixed_color (uint8_t data[BLOCK],
                                     const uint8_t rgb[3]);
static void edit_global_palette (uint8_t data[BLOCK],
                                const uint8_t colors[8][3]);
static bool edit_dpi_color (uint8_t data[BLOCK], unsigned slot,
                            const uint8_t rgb[3]);

static void
scroll_set_direction (uint8_t data[BLOCK], bool natural)
{
  /* Normal: rec14=up, rec15=down. Natural: rec14=down, rec15=up. */
  data[SCROLL_REC14 + 2] = natural ? SCROLL_DOWN : SCROLL_UP;
  data[SCROLL_REC15 + 2] = natural ? SCROLL_UP : SCROLL_DOWN;
}

/* Returns true if the two scroll records are type 0x04 and in a known state.
 */
static bool
scroll_is_valid (const uint8_t data[BLOCK])
{
  bool rec14 = data[SCROLL_REC14] == SCROLL_TYPE;
  bool rec15 = data[SCROLL_REC15] == SCROLL_TYPE;
  bool up14 = data[SCROLL_REC14 + 2] == SCROLL_UP;
  bool dn14 = data[SCROLL_REC14 + 2] == SCROLL_DOWN;
  bool up15 = data[SCROLL_REC15 + 2] == SCROLL_UP;
  bool dn15 = data[SCROLL_REC15 + 2] == SCROLL_DOWN;
  bool normal = rec14 && rec15 && up14 && dn15;
  bool natural = rec14 && rec15 && dn14 && up15;
  return normal || natural;
}

static bool
scroll_is_natural (const uint8_t data[BLOCK])
{
  return data[SCROLL_REC14] == SCROLL_TYPE
         && data[SCROLL_REC14 + 2] == SCROLL_DOWN
         && data[SCROLL_REC15] == SCROLL_TYPE
         && data[SCROLL_REC15 + 2] == SCROLL_UP;
}

static uint32_t
checksum (const uint8_t *data, size_t size)
{
  uint32_t hash = UINT32_C (2166136261);
  for (size_t i = 0; i < size; i++)
    hash = (hash ^ data[i]) * UINT32_C (16777619);
  return hash;
}

/* Full-device snapshot: 9-byte magic + 2-byte version + 6*(config+buttons) +
 * crc. */
static void
pack_snapshot (uint8_t out[SNAP_SIZE], const uint8_t *blocks)
{
  memset (out, 0, SNAP_SIZE);
  memcpy (out, "A22ASNAP", 9);
  out[9] = 0x01;
  out[10] = 0x00; /* format version 1, little endian */
  size_t off = 11;
  for (unsigned p = 0; p < PROFILES; p++)
    {
      memcpy (out + off, blocks + p * 2 * BLOCK, BLOCK);
      off += BLOCK;
      memcpy (out + off, blocks + p * 2 * BLOCK + BLOCK, BLOCK);
      off += BLOCK;
    }
  uint32_t hash = checksum (out, SNAP_SIZE - 4);
  for (unsigned i = 0; i < 4; i++)
    out[SNAP_SIZE - 4 + i] = (uint8_t)(hash >> (8 * i));
}

static bool
valid_snapshot (const uint8_t data[SNAP_SIZE])
{
  uint32_t stored = 0;
  for (unsigned i = 0; i < 4; i++)
    stored |= (uint32_t)data[SNAP_SIZE - 4 + i] << (8 * i);
  return !memcmp (data, "A22ASNAP", 9) && data[9] == 0x01 && data[10] == 0x00
         && stored == checksum (data, SNAP_SIZE - 4);
}

static void
unpack_snapshot (const uint8_t data[SNAP_SIZE], uint8_t *blocks)
{
  size_t off = 11;
  for (unsigned p = 0; p < PROFILES; p++)
    {
      memcpy (blocks + p * 2 * BLOCK, data + off, BLOCK);
      off += BLOCK;
      memcpy (blocks + p * 2 * BLOCK + BLOCK, data + off, BLOCK);
      off += BLOCK;
    }
}

static int
read_snapshot (int fd, uint8_t out[SNAP_SIZE])
{
  uint8_t blocks[PROFILES][2][BLOCK];
  for (unsigned p = 0; p < PROFILES; p++)
    {
      if (read_profile (fd, p, blocks[p][0])
          || read_buttons (fd, p, blocks[p][1]))
        return -1;
    }
  pack_snapshot (out, (const uint8_t *)blocks);
  return 0;
}

static void
save_snapshot (const uint8_t data[SNAP_SIZE], const char *path)
{
  int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0)
    fail ("Cannot create snapshot file.");
  size_t pos = 0;
  while (pos < SNAP_SIZE)
    {
      ssize_t n = write (fd, data + pos, SNAP_SIZE - pos);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        fail ("Cannot write snapshot file.");
      pos += (size_t)n;
    }
  if (fsync (fd) || close (fd))
    fail ("Cannot flush snapshot file.");
}

static void
load_snapshot (const char *path, uint8_t data[SNAP_SIZE])
{
  uint8_t input[SNAP_SIZE + 1];
  FILE *file = fopen (path, "rb");
  if (!file)
    fail ("Cannot open snapshot.");
  size_t n = fread (input, 1, sizeof (input), file);
  bool ok = !ferror (file) && n == SNAP_SIZE && valid_snapshot (input);
  fclose (file);
  if (!ok)
    fail ("Snapshot invalid (bad format or checksum). Nothing written.");
  memcpy (data, input, SNAP_SIZE);
}

static int
open_mouse (void)
{
  glob_t paths = { 0 };
  int selected = -1;
  if (glob ("/sys/bus/hid/devices/0003:04D9:A22A.*/hidraw/hidraw*", 0, NULL,
            &paths))
    fail ("No A22A hidraw nodes found.");
  for (size_t i = 0; i < paths.gl_pathc; i++)
    {
      const char *name = strrchr (paths.gl_pathv[i], '/') + 1;
      char path[256];
      snprintf (path, sizeof (path), "/dev/%s", name);
      int fd = open (path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0)
        continue;
      struct hidraw_devinfo info = { 0 };
      int descriptor_size = 0;
      struct hidraw_report_descriptor desc = { .size = sizeof (descriptor) };
      if (ioctl (fd, HIDIOCGRAWINFO, &info) || info.bustype != BUS_USB
          || (uint16_t)info.vendor != 0x04d9
          || (uint16_t)info.product != 0xa22a
          || ioctl (fd, HIDIOCGRDESCSIZE, &descriptor_size)
          || descriptor_size != (int)sizeof (descriptor)
          || ioctl (fd, HIDIOCGRDESC, &desc)
          || memcmp (desc.value, descriptor, sizeof (descriptor)))
        {
          close (fd);
          continue;
        }
      snprintf (path, sizeof (path),
                "/sys/class/hidraw/%s/device/../../bcdDevice", name);
      FILE *revision = fopen (path, "r");
      unsigned value = 0;
      if (revision)
        {
          if (fscanf (revision, "%x", &value) != 1)
            value = 0;
          fclose (revision);
        }
      if (value != 0x0101)
        fail ("Unverified bcdDevice revision; refusing access.");
      if (selected >= 0)
        fail ("Multiple matching mice; connect only one A22A.");
      selected = fd;
    }
  globfree (&paths);
  if (selected < 0)
    fail ("Cannot open matching configuration interface; try sudo.");
  if (flock (selected, LOCK_EX | LOCK_NB))
    fail ("Another cooperating process holds this device lock.");
  return selected;
}

/* The firmware can corrupt the global profile-0 block (sensor init fields)
 * when other blocks are written. These helpers snapshot it before a write and
 * restore it afterward if it changed unexpectedly. No offsets or values are
 * hardcoded. */
static int
snapshot_global (int fd, uint8_t saved[BLOCK])
{
  return read_profile (fd, 0, saved);
}

static int
restore_global_if_changed (int fd, const uint8_t saved[BLOCK],
                           const char *what)
{
  uint8_t cur[BLOCK];
  if (read_profile (fd, 0, cur))
    return -1;
  if (memcmp (cur, saved, BLOCK) == 0)
    return 0;
  fprintf (stderr,
           "Firmware corrupted profile 0 during %s; restoring snapshot.\n",
           what);
  if (write_profile (fd, 0, saved))
    return -1;
  if (read_profile (fd, 0, cur) || memcmp (cur, saved, BLOCK))
    return -1;
  printf (
      "Restored profile 0 global configuration after firmware corruption.\n");
  return 0;
}

/* Full guarded configuration-table write: preflight re-read, write,
 * readback, reselect current slot, final readback. `slot` is the wire slot to
 * reselect after the write; `target` is the intended full 128-byte block. */
static void
commit_profile_for_active (int fd, uint8_t data_profile,
                           uint8_t active_profile, uint8_t slot,
                           const uint8_t original[BLOCK],
                           const uint8_t target[BLOCK])
{
  uint8_t verify[BLOCK];
  uint8_t global0[BLOCK];
  bool guard = data_profile != 0;
  fprintf (stderr,
           "Preparing configuration write; all unrelated fields preserved.\n");
  if (guard && snapshot_global (fd, global0))
    fail ("Cannot snapshot global config; no write attempted.");
  uint8_t check_profile, check_slot;
  if (current (fd, &check_profile, &check_slot)
      || check_profile != active_profile || check_slot != slot
      || read_profile (fd, data_profile, verify)
      || memcmp (verify, original, BLOCK))
    fail ("State changed during preparation; nothing written.");
  if (write_profile (fd, data_profile, target))
    fail ("WRITE FAILED; state may be partial. Do not retry blindly.");
  if (read_profile (fd, data_profile, verify)
      || memcmp (verify, target, BLOCK))
    fail ("READBACK FAILED; no activation or automatic rollback attempted.");
  if (send_command (fd, 0x04, active_profile, slot)
      || current (fd, &check_profile, &check_slot)
      || check_profile != active_profile
      || check_slot != slot)
    fail ("Configuration written, but activation unverified.");
  if (read_profile (fd, data_profile, verify)
      || memcmp (verify, target, BLOCK))
    fail ("Post-activation readback differs. Inspect before further writes.");
  if (guard && restore_global_if_changed (fd, global0, "profile write"))
    fail ("GLOBAL RESTORE FAILED; profile 0 may remain corrupted.");
}

static void
commit_profile (int fd, uint8_t profile, uint8_t slot,
                const uint8_t original[BLOCK], const uint8_t target[BLOCK])
{
  commit_profile_for_active (fd, profile, profile, slot, original, target);
}

static int
self_test (void)
{
  uint8_t data[BLOCK], changed[BLOCK], p[9], snap[SNAP_SIZE];
  uint8_t blocks[PROFILES][2][BLOCK], out[PROFILES][2][BLOCK];
  packet (p, 0x8c, 1, 0);
  if (memcmp (p, (uint8_t[]){ 0, 0x8c, 1, 0, 0, 0, 0, 0, 0x72 }, 9))
    return 1;
  packet (p, 0x0c, 1, BLOCK);
  if (p[8] != 0x72)
    return 1;
  for (unsigned i = 0; i < BLOCK; i++)
    data[i] = (uint8_t)(i * 17);
  for (unsigned slot = 1; slot <= 8; slot++)
    {
      for (unsigned dpi = MIN_DPI; dpi <= MAX_DPI; dpi += 100)
        {
          memcpy (changed, data, BLOCK);
          edit_dpi (changed, slot, dpi);
          unsigned encoded = dpi / 100;
          unsigned estimate
              = encoded < 63 ? dpi : (encoded / 2) * 200;
          unsigned decoded;
          if (raw_dpi (changed, slot, false) != encode_dpi (dpi)
              || !decode_dpi (raw_dpi (changed, slot, false), &decoded)
              || decoded != estimate
              || raw_dpi (changed, slot, true) != raw_dpi (data, slot, true))
            return 1;
          for (unsigned i = 0; i < BLOCK; i++)
            {
              if (i != 84 + slot - 1 && changed[i] != data[i])
                return 1;
            }
      }
    }
  memcpy (changed, data, BLOCK);
  if (encode_dpi (100) != 2 || encode_dpi (6200) != 62
      || encode_dpi (6300) != 0x5f || encode_dpi (6400) != 0x60
      || encode_dpi (8000) != 0x68 || encode_dpi (MAX_DPI) != 0x7e)
    return 1;
  unsigned decoded;
  if (!decode_dpi (0x68, &decoded) || decoded != 8000
      || !decode_dpi (0x5f, &decoded) || decoded != 6200
      || decode_dpi (0xe0, &decoded))
    return 1;
  edit_dpi (changed, 1, MAX_DPI);
  if (raw_dpi (changed, 1, false) != 0x7e)
    return 1;
  static const struct
  {
    unsigned mode, a, b;
    uint8_t byte_a, byte_b;
  } illumination_cases[] = {
    { ILLUM_OFF, 0, 0, 0, 0 },
    { ILLUM_STANDARD, 0x44, 0, 0x44, 0 },
    { ILLUM_TWINKLE, 0x12, 0, 0x12, 0 },
    { ILLUM_BREATHING, 0, 3, 0, 8 },
    { ILLUM_NEON, 0, 11, 0, 0 },
    { ILLUM_WAVE, 0x45, 0x67, 0x45, 0x67 },
    { ILLUM_SLIDE, 0, 0, 0, 0 },
    { ILLUM_FINGER, 0x22, 0, 0x22, 0 },
    { ILLUM_FLIP, 0x35, 4, 0x35, 0x10 },
    { ILLUM_YOYO, 7, 5, 7, 10 },
    { ILLUM_CROSS, 7, 6, 7, 24 },
    { ILLUM_FLYING, 0, 0, 0, 0 }
  };
  for (unsigned i = 0;
       i < sizeof (illumination_cases) / sizeof (illumination_cases[0]); i++)
    {
      for (unsigned j = 0; j < BLOCK; j++)
        data[j] = (uint8_t)(j * 11 + 3);
      memcpy (changed, data, BLOCK);
      if (!edit_illumination (changed, illumination_cases[i].mode,
                              illumination_cases[i].a,
                              illumination_cases[i].b)
          || changed[ILLUM_MODE] != illumination_cases[i].mode
          || changed[ILLUM_PARAM_A] != illumination_cases[i].byte_a
          || changed[ILLUM_PARAM_B] != illumination_cases[i].byte_b
          || strcmp (illum_mode_name (changed[ILLUM_MODE]), "unknown") == 0)
        return 1;
      for (unsigned j = 0; j < BLOCK; j++)
        if (j != ILLUM_MODE && j != ILLUM_PARAM_A && j != ILLUM_PARAM_B
            && changed[j] != data[j])
          return 1;
    }
  uint8_t byte_a, byte_b;
  if (illum_mode_from_name ("standard") != ILLUM_STANDARD
      || illum_mode_from_name ("yo-yo") != ILLUM_YOYO
      || illum_mode_argument_count (ILLUM_OFF) != 0
      || illum_mode_argument_count (ILLUM_BREATHING) != 1
      || illum_mode_argument_count (ILLUM_WAVE) != 2
      || encode_illumination_parameters (ILLUM_BREATHING, 0, 12, &byte_a,
                                         &byte_b)
      || encode_illumination_parameters (ILLUM_FLIP, 1, 21, &byte_a,
                                         &byte_b)
      || encode_illumination_parameters (ILLUM_YOYO, 1, 16, &byte_a,
                                         &byte_b)
      || encode_illumination_parameters (ILLUM_CROSS, 1, 31, &byte_a,
                                         &byte_b)
      || encode_illumination_parameters (ILLUM_MODE_COUNT, 0, 0, &byte_a,
                                         &byte_b))
    return 1;
  uint8_t rgb[3], palette[8][3], color_original[BLOCK], color_changed[BLOCK];
  if (!parse_rgb ("#a10FfF", rgb) || rgb[0] != 0xa1 || rgb[1] != 0x0f
      || rgb[2] != 0xff || parse_rgb ("12G456", rgb)
      || parse_rgb ("12345", rgb))
    return 1;
  for (unsigned i = 0; i < BLOCK; i++)
    color_original[i] = (uint8_t)(i * 13 + 9);
  memcpy (color_changed, color_original, BLOCK);
  edit_global_fixed_color (color_changed, rgb);
  for (unsigned i = 0; i < BLOCK; i++)
    {
      uint8_t expected = color_original[i];
      if (i >= ILLUM_COLOR && i < ILLUM_COLOR + 24)
        expected = rgb[(i - ILLUM_COLOR) % 3];
      if (color_changed[i] != expected)
        return 1;
    }
  for (unsigned s = 0; s < 8; s++)
    for (unsigned c = 0; c < 3; c++)
      palette[s][c] = (uint8_t)(s * 29 + c * 7);
  memcpy (color_changed, color_original, BLOCK);
  edit_global_palette (color_changed, palette);
  for (unsigned i = 0; i < BLOCK; i++)
    {
      uint8_t expected = color_original[i];
      if (i >= ILLUM_COLOR && i < ILLUM_COLOR + 24)
        expected = palette[(i - ILLUM_COLOR) / 3][(i - ILLUM_COLOR) % 3];
      if (color_changed[i] != expected)
        return 1;
    }
  for (unsigned slot = 1; slot <= 8; slot++)
    {
      memcpy (color_changed, color_original, BLOCK);
      if (!edit_dpi_color (color_changed, slot, rgb))
        return 1;
      for (unsigned i = 0; i < BLOCK; i++)
        {
          unsigned offset = DPI_COLOR + (slot - 1) * 3;
          uint8_t expected = i >= offset && i < offset + 3
                                 ? rgb[i - offset]
                                 : color_original[i];
          if (color_changed[i] != expected)
            return 1;
        }
    }
  if (edit_dpi_color (color_changed, 0, rgb)
      || edit_dpi_color (color_changed, 9, rgb))
    return 1;
  for (unsigned pr = 0; pr < PROFILES; pr++)
    for (unsigned b = 0; b < 2; b++)
      for (unsigned i = 0; i < BLOCK; i++)
        blocks[pr][b][i] = (uint8_t)(pr * 37 + b * 53 + i);
  pack_snapshot (snap, (const uint8_t *)blocks);
  if (!valid_snapshot (snap))
    return 1;
  unpack_snapshot (snap, (uint8_t *)out);
  for (unsigned pr = 0; pr < PROFILES; pr++)
    for (unsigned b = 0; b < 2; b++)
      if (memcmp (out[pr][b], blocks[pr][b], BLOCK))
        return 1;
  for (unsigned i = 0; i < SNAP_SIZE; i++)
    {
      snap[i] ^= 1;
      if (valid_snapshot (snap))
        return 1;
      snap[i] ^= 1;
    }
  puts ("PASS: packets, DPI codec, OEM LED mode/color maps, "
        "unrelated-byte preservation, snapshot roundtrip/corruption.");
  return 0;
}

static void
print_usage (FILE *out, const char *prog)
{
  fprintf (out,
           "Usage:\n  %s [show [PROFILE]]\n  %s --self-test\n"
           "  %s dpi DPI\n"
           "  %s slot [-c|--count] SLOT\n"
           "  %s count N\n"
           "  %s rate HZ\n"
           "  %s profile [-d|--duplicate SRC] DST\n"
           "  %s wheel\n"
           "  %s snapshot FILE\n"
           "  %s restore FILE\n"
           "  %s led mode MODE [A [B]]\n"
           "  %s led color fixed RRGGBB\n"
           "  %s led color palette RGB1 ... RGB8\n"
           "  %s led color dpi SLOT RRGGBB\n"
           "  %s help|-h|--help\n"
           "Sensor=3327 DPI encoding: 200..12400 by 100; high odd steps may "
           "round down.\n"
           "Rate options: 125, 250, 500, 1000. Profiles: 0..5.\n"
           "LED modes use OEM indices: off, standard, twinkle, breathing, "
           "neon, wave, slide, finger, flip, yoyo, cross, flying.\n"
           "Mode-specific A/B parameters are required; see README.\n"
           "RGB accepts six hex digits, with an optional leading #.\n"
           "Writes are immediate and may persist. Close vendor software; do "
           "not unplug.\n",
           prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog,
           prog, prog, prog, prog);
}

static const char *
illum_mode_name (uint8_t mode)
{
  return mode < ILLUM_MODE_COUNT ? illum_mode_labels[mode] : "unknown";
}

static int
illum_mode_from_name (const char *name)
{
  for (unsigned i = 0; i < ILLUM_MODE_COUNT; i++)
    if (!strcmp (name, illum_mode_cli_names[i]))
      return (int)i;
  if (!strcmp (name, "finger-movement"))
    return ILLUM_FINGER;
  if (!strcmp (name, "flip-up-down"))
    return ILLUM_FLIP;
  if (!strcmp (name, "yo-yo"))
    return ILLUM_YOYO;
  if (!strcmp (name, "cross-flash"))
    return ILLUM_CROSS;
  if (!strcmp (name, "flying-star"))
    return ILLUM_FLYING;
  return -1;
}

/* Number of OEM A/B arguments accepted by each mode. Modes 3/4 consume B
 * only; all other one-argument modes consume A. */
static int
illum_mode_argument_count (unsigned mode)
{
  switch (mode)
    {
    case ILLUM_OFF:
    case ILLUM_SLIDE:
    case ILLUM_FLYING:
      return 0;
    case ILLUM_STANDARD:
    case ILLUM_TWINKLE:
    case ILLUM_BREATHING:
    case ILLUM_NEON:
    case ILLUM_FINGER:
      return 1;
    case ILLUM_WAVE:
    case ILLUM_FLIP:
    case ILLUM_YOYO:
    case ILLUM_CROSS:
      return 2;
    default:
      return -1;
    }
}

static bool
illum_mode_uses_b_only (unsigned mode)
{
  return mode == ILLUM_BREATHING || mode == ILLUM_NEON;
}

/* Encode the OEM profile-writer parameters for offsets 72/73. A and B are
 * internal writer inputs, not direct Windows slider values. */
static bool
encode_illumination_parameters (unsigned mode, unsigned a, unsigned b,
                               uint8_t *byte_a, uint8_t *byte_b)
{
  if (a > 255 || b > 255)
    return false;
  switch (mode)
    {
    case ILLUM_OFF:
    case ILLUM_SLIDE:
    case ILLUM_FLYING:
      if (a || b)
        return false;
      *byte_a = 0;
      *byte_b = 0;
      return true;
    case ILLUM_STANDARD:
    case ILLUM_TWINKLE:
    case ILLUM_FINGER:
      if (b)
        return false;
      *byte_a = (uint8_t)a;
      *byte_b = 0;
      return true;
    case ILLUM_BREATHING:
    case ILLUM_NEON:
      if (a || b > 0x0b)
        return false;
      *byte_a = 0;
      *byte_b = (uint8_t)(0x0b - b);
      return true;
    case ILLUM_WAVE:
      *byte_a = (uint8_t)a;
      *byte_b = (uint8_t)b;
      return true;
    case ILLUM_FLIP:
      if (b > 0x14)
        return false;
      *byte_a = (uint8_t)a;
      *byte_b = (uint8_t)(0x14 - b);
      return true;
    case ILLUM_YOYO:
      if (b > 0x0f)
        return false;
      *byte_a = (uint8_t)a;
      *byte_b = (uint8_t)(0x0f - b);
      return true;
    case ILLUM_CROSS:
      if (b > 0x1e)
        return false;
      *byte_a = (uint8_t)a;
      *byte_b = (uint8_t)(0x1e - b);
      return true;
    default:
      return false;
    }
}

static bool
edit_illumination (uint8_t data[BLOCK], unsigned mode, unsigned a,
                   unsigned b)
{
  uint8_t byte_a, byte_b;
  if (!encode_illumination_parameters (mode, a, b, &byte_a, &byte_b))
    return false;
  data[ILLUM_MODE] = (uint8_t)mode;
  data[ILLUM_PARAM_A] = byte_a;
  data[ILLUM_PARAM_B] = byte_b;
  return true;
}

static int
hex_digit (char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool
parse_rgb (const char *text, uint8_t rgb[3])
{
  if (*text == '#')
    text++;
  if (strlen (text) != 6)
    return false;
  for (unsigned i = 0; i < 3; i++)
    {
      int high = hex_digit (text[i * 2]);
      int low = hex_digit (text[i * 2 + 1]);
      if (high < 0 || low < 0)
        return false;
      rgb[i] = (uint8_t)(high * 16 + low);
    }
  return true;
}

static void
edit_global_fixed_color (uint8_t data[BLOCK], const uint8_t rgb[3])
{
  for (unsigned i = 0; i < 8; i++)
    memcpy (data + ILLUM_COLOR + i * 3, rgb, 3);
}

static void
edit_global_palette (uint8_t data[BLOCK], const uint8_t colors[8][3])
{
  memcpy (data + ILLUM_COLOR, colors, 8 * 3);
}

static bool
edit_dpi_color (uint8_t data[BLOCK], unsigned slot, const uint8_t rgb[3])
{
  if (slot < 1 || slot > 8)
    return false;
  memcpy (data + DPI_COLOR + (slot - 1) * 3, rgb, 3);
  return true;
}

static unsigned
parse_byte_arg (const char *s)
{
  char *end;
  errno = 0;
  unsigned long v = strtoul (s, &end, 10);
  if (errno || end == s || *end || v > 255)
    fail ("Argument must be 0..255.");
  return (unsigned)v;
}

/* Print the lighting fields of the active profile's configuration block. All
 * values are read-only and candidate; nothing is normalized or written. */
static void
print_lighting (const uint8_t data[BLOCK], uint8_t profile)
{
  printf ("Illumination mode=%u (%s), raw params 72/73=%u/%u\n",
          data[ILLUM_MODE], illum_mode_name (data[ILLUM_MODE]),
          data[ILLUM_PARAM_A], data[ILLUM_PARAM_B]);
  if (profile == 0)
    {
      printf ("Profile-0 global lighting:");
      for (unsigned s = 0; s < 8; s++)
        printf (" %u:#%02x%02x%02x", s + 1, data[ILLUM_COLOR + s * 3],
                data[ILLUM_COLOR + s * 3 + 1], data[ILLUM_COLOR + s * 3 + 2]);
      printf ("\nProfile-0 DPI-indicator enable:");
      for (unsigned s = 0; s < 8; s++)
        printf (" %02x", data[INDICATOR_ENABLE + s]);
      printf ("\n");
    }
}

/* Print a profile's state. If profile_index is negative, use the active
 * profile; otherwise read the requested profile block without switching. */
static void
print_state (int fd, bool verbose, int profile_index)
{
  uint8_t profile, slot;
  uint8_t data[BLOCK], buttons[BLOCK], rate_raw;
  unsigned x_estimate;
  if (current (fd, &profile, &slot))
    fail ("Cannot read current state.");
  if (profile_index >= 0)
    profile = (uint8_t)profile_index;
  if (read_profile (fd, profile, data))
    fail ("Cannot read profile state.");
  printf (
      "Device 04d9:a22a revision 0101; profile=%u, slot=%u (wire indices)\n",
      profile, slot);
  if (estimate_slot_dpi (data, slot, &x_estimate))
    printf ("Current X raw=0x%02x, Sensor=3327 code estimate=%u CPI; "
            "Y raw=0x%02x (not decoded).\n",
            raw_dpi (data, slot, false), x_estimate,
            raw_dpi (data, slot, true));
  else
    printf ("Current X raw=0x%02x (estimate unavailable); Y raw=0x%02x "
            "(not decoded).\n",
            raw_dpi (data, slot, false), raw_dpi (data, slot, true));
  printf ("Candidate count=%u, enabled mask=0x%02x, scale X/Y=%u/%u\n",
          data[70], data[100], data[74], data[75]);
  if (get_rate (fd, profile, &rate_raw) == 0)
    printf ("Report rate raw=0x%02x (~%u Hz); enabled-rates field=0x%02x\n",
            rate_raw, raw_to_rate_hz (rate_raw), data[64]);
  else
    printf ("Report rate query failed; enabled-rates field=0x%02x\n",
            data[64]);
  if (read_buttons (fd, profile, buttons) == 0 && scroll_is_valid (buttons))
    printf ("Wheel direction: %s\n",
            scroll_is_natural (buttons) ? "natural" : "normal");
  else
    printf ("Wheel direction: unknown (scroll records unrecognized)\n");
  print_lighting (data, profile);
  printf ("Sensor model unverified (OEM config: 3327); the displayed CPI is an "
          "encoding estimate, not physical calibration.\n"
          "Unverified high masks X/Y=0x%02x/0x%02x.\n",
          data[82], data[83]);
  if (verbose)
    for (unsigned s = 1; s <= 8; s++)
      {
        unsigned estimate;
        if (estimate_slot_dpi (data, s, &estimate))
          printf ("slot %u: X raw=0x%02x (~%u CPI estimate), Y raw=0x%02x, "
                  "#%02x%02x%02x, enabled-by-count-and-mask=%s\n",
                  s, raw_dpi (data, s, false), estimate,
                  raw_dpi (data, s, true), data[DPI_COLOR + (s - 1) * 3],
                  data[DPI_COLOR + (s - 1) * 3 + 1],
                  data[DPI_COLOR + (s - 1) * 3 + 2],
                  s <= data[70] && (data[100] & (1u << (s - 1))) ? "yes"
                                                                  : "no");
        else
          printf ("slot %u: X raw=0x%02x (unmapped), Y raw=0x%02x, "
                  "#%02x%02x%02x, enabled-by-count-and-mask=%s\n",
                  s, raw_dpi (data, s, false), raw_dpi (data, s, true),
                  data[DPI_COLOR + (s - 1) * 3],
                  data[DPI_COLOR + (s - 1) * 3 + 1],
                  data[DPI_COLOR + (s - 1) * 3 + 2],
                  s <= data[70] && (data[100] & (1u << (s - 1))) ? "yes"
                                                                  : "no");
      }
}

int
main (int argc, char **argv)
{
  if (argc == 2 && !strcmp (argv[1], "--self-test"))
    return self_test ();
  bool help = argc == 2
              && (!strcmp (argv[1], "help") || !strcmp (argv[1], "usage")
                  || !strcmp (argv[1], "-h") || !strcmp (argv[1], "--help")
                  || !strcmp (argv[1], "--usage"));
  if (help)
    {
      print_usage (stdout, argv[0]);
      return 0;
    }
  bool show = argc == 1 || (argc == 2 && !strcmp (argv[1], "show"));
  bool show_profile = argc == 3 && !strcmp (argv[1], "show");
  bool set_dpi = argc == 3 && !strcmp (argv[1], "dpi");
  bool snapshot = argc == 3 && !strcmp (argv[1], "snapshot");
  bool restore = argc == 3 && !strcmp (argv[1], "restore");
  bool flip_wheel = argc == 2 && !strcmp (argv[1], "wheel");
  bool set_rate = argc == 3 && !strcmp (argv[1], "rate");
  bool slot_cmd = argc == 3 && !strcmp (argv[1], "slot");
  bool slot_count
      = argc == 4 && !strcmp (argv[1], "slot")
        && (!strcmp (argv[2], "-c") || !strcmp (argv[2], "--count"));
  bool set_count = argc == 3 && !strcmp (argv[1], "count");
  bool profile_cmd = argc == 3 && !strcmp (argv[1], "profile");
  bool profile_dup
      = argc == 5 && !strcmp (argv[1], "profile")
        && (!strcmp (argv[2], "-d") || !strcmp (argv[2], "--duplicate"));
  bool led_mode
      = argc >= 4 && argc <= 6 && !strcmp (argv[1], "led")
        && !strcmp (argv[2], "mode");
  bool led_color_fixed
      = argc == 5 && !strcmp (argv[1], "led") && !strcmp (argv[2], "color")
        && !strcmp (argv[3], "fixed");
  bool led_color_palette
      = argc == 12 && !strcmp (argv[1], "led") && !strcmp (argv[2], "color")
        && !strcmp (argv[3], "palette");
  bool led_color_dpi
      = argc == 6 && !strcmp (argv[1], "led") && !strcmp (argv[2], "color")
        && !strcmp (argv[3], "dpi");
  bool led_color = led_color_fixed || led_color_palette || led_color_dpi;
  if (!show && !show_profile && !set_dpi && !snapshot && !restore
      && !flip_wheel && !set_rate && !slot_cmd && !slot_count && !set_count
      && !profile_cmd && !profile_dup && !led_mode && !led_color)
    {
      print_usage (stderr, argv[0]);
      return 2;
    }
  int show_index = -1;
  if (show_profile)
    {
      char *end;
      errno = 0;
      unsigned long value = strtoul (argv[2], &end, 10);
      if (errno || end == argv[2] || *end || value >= PROFILES)
        fail ("Profile must be 0..5; device not opened.");
      show_index = (int)value;
    }
  unsigned dpi = 0;
  if (set_dpi)
    {
      char *end;
      errno = 0;
      unsigned long value = strtoul (argv[2], &end, 10);
      if (errno || end == argv[2] || *end || value < MIN_DPI || value > MAX_DPI
          || value % 100)
        fail ("DPI must be 200..12400 in steps of 100; device not opened.");
      dpi = (unsigned)value;
    }
  unsigned target_slot = 0;
  if (slot_cmd || slot_count)
    {
      const char *slot_arg = slot_count ? argv[3] : argv[2];
      char *end;
      errno = 0;
      unsigned long value = strtoul (slot_arg, &end, 10);
      if (errno || end == slot_arg || *end || value < 1 || value > 8)
        fail ("Slot must be 1..8; device not opened.");
      target_slot = (unsigned)value;
    }
  unsigned target_count = 0;
  if (set_count)
    {
      char *end;
      errno = 0;
      unsigned long value = strtoul (argv[2], &end, 10);
      if (errno || end == argv[2] || *end || value < 1 || value > 8)
        fail ("Count must be 1..8; device not opened.");
      target_count = (unsigned)value;
    }
  unsigned target_profile = 0;
  if (profile_cmd)
    {
      char *end;
      errno = 0;
      unsigned long value = strtoul (argv[2], &end, 10);
      if (errno || end == argv[2] || *end || value >= PROFILES)
        fail ("Profile must be 0..5; device not opened.");
      target_profile = (unsigned)value;
    }
  unsigned copy_src = 0, copy_dst = 0;
  if (profile_dup)
    {
      char *end;
      errno = 0;
      unsigned long s = strtoul (argv[3], &end, 10);
      if (errno || end == argv[3] || *end || s >= PROFILES)
        fail ("Source profile must be 0..5; device not opened.");
      copy_src = (unsigned)s;
      errno = 0;
      unsigned long d = strtoul (argv[4], &end, 10);
      if (errno || end == argv[4] || *end || d >= PROFILES)
        fail ("Destination profile must be 0..5; device not opened.");
      copy_dst = (unsigned)d;
      if (copy_src == copy_dst)
        fail (
            "Source and destination profiles must differ; device not opened.");
    }
  unsigned target_hz = 0;
  if (set_rate)
    {
      char *end;
      errno = 0;
      unsigned long value = strtoul (argv[2], &end, 10);
      if (errno || end == argv[2] || *end || !rate_to_raw ((unsigned)value))
        fail ("Rate must be 125, 250, 500 or 1000; device not opened.");
      target_hz = (unsigned)value;
    }
  unsigned led_mode_value = 0, led_param_a = 0, led_param_b = 0;
  uint8_t led_byte_a = 0, led_byte_b = 0;
  if (led_mode)
    {
      int mode = illum_mode_from_name (argv[3]);
      if (mode < 0)
        fail ("Unknown LED mode; use off, standard, twinkle, breathing, neon, "
              "wave, slide, finger, flip, yoyo, cross or flying.");
      led_mode_value = (unsigned)mode;
      int parameter_count = illum_mode_argument_count (led_mode_value);
      if (argc != 4 + parameter_count)
        fail ("Wrong number of LED parameters for this mode; device not "
              "opened.");
      if (parameter_count == 1)
        {
          unsigned value = parse_byte_arg (argv[4]);
          if (illum_mode_uses_b_only (led_mode_value))
            led_param_b = value;
          else
            led_param_a = value;
        }
      else if (parameter_count == 2)
        {
          led_param_a = parse_byte_arg (argv[4]);
          led_param_b = parse_byte_arg (argv[5]);
        }
      if (!encode_illumination_parameters (led_mode_value, led_param_a,
                                           led_param_b, &led_byte_a,
                                           &led_byte_b))
        fail ("LED B parameter is outside the range supported by this mode; "
              "device not opened.");
    }
  unsigned led_color_slot = 0;
  uint8_t led_colors[8][3] = { { 0 } };
  if (led_color_fixed)
    {
      if (!parse_rgb (argv[4], led_colors[0]))
        fail ("RGB color must be six hex digits (RRGGBB); device not opened.");
    }
  if (led_color_palette)
    for (unsigned i = 0; i < 8; i++)
      if (!parse_rgb (argv[4 + i], led_colors[i]))
        fail ("Each palette entry must be six hex digits (RRGGBB); device not "
              "opened.");
  if (led_color_dpi)
    {
      char *end;
      errno = 0;
      unsigned long value = strtoul (argv[4], &end, 10);
      if (errno || end == argv[4] || *end || value < 1 || value > 8)
        fail ("DPI slot must be 1..8; device not opened.");
      led_color_slot = (unsigned)value;
      if (!parse_rgb (argv[5], led_colors[0]))
        fail ("RGB color must be six hex digits (RRGGBB); device not opened.");
    }
  int fd = open_mouse ();
  uint8_t profile, slot, original[BLOCK], target[BLOCK];
  if (current (fd, &profile, &slot) || read_profile (fd, profile, original))
    fail ("Cannot read current state; no configuration write attempted.");
  if (show || show_profile)
    {
      print_state (fd, true, show_index);
      close (fd);
      return 0;
    }
  if (profile_cmd)
    {
      if (target_profile == profile)
        {
          printf ("Profile %u already active; no write performed.\n", profile);
          close (fd);
          return 0;
        }
      if (set_active_profile (fd, (uint8_t)target_profile))
        fail ("Profile switch unverified; current state unchanged or "
              "uncertain.");
      printf (
          "Active profile changed to %u.\n"
          "Applied via command 02; no configuration block was rewritten.\n",
          target_profile);
      print_state (fd, true, -1);
      close (fd);
      return 0;
    }
  if (profile_dup)
    {
      uint8_t src_cfg[BLOCK], src_btn[BLOCK], global0[BLOCK];
      if (snapshot_global (fd, global0))
        fail ("Cannot snapshot global config; no write attempted.");
      if (read_profile (fd, (uint8_t)copy_src, src_cfg)
          || read_buttons (fd, (uint8_t)copy_src, src_btn))
        fail ("Cannot read source profile; no write attempted.");
      /* Copy config and button blocks to the destination profile. */
      uint8_t dst_cur[BLOCK];
      if (read_profile (fd, (uint8_t)copy_dst, dst_cur) == 0
          && memcmp (dst_cur, src_cfg, BLOCK) != 0)
        write_profile (fd, (uint8_t)copy_dst, src_cfg);
      if (read_buttons (fd, (uint8_t)copy_dst, dst_cur) == 0
          && memcmp (dst_cur, src_btn, BLOCK) != 0)
        write_buttons (fd, (uint8_t)copy_dst, src_btn);
      if (restore_global_if_changed (fd, global0, "profile copy"))
        fail ("GLOBAL RESTORE FAILED; profile 0 may remain corrupted.");
      printf ("Copied profile %u config+buttons to profile %u.\n", copy_src,
              copy_dst);
      print_state (fd, true, -1);
      close (fd);
      return 0;
    }
  if (slot_count && target_slot > original[70])
    {
      if (original[74] != 100 || original[75] != 100)
        fail ("State outside inspected DPI layout; nothing written.");
      target_count = target_slot;
      memcpy (target, original, BLOCK);
      target[70] = (uint8_t)target_count;
      if (memcmp (original, target, BLOCK) != 0)
        {
          commit_profile (fd, profile, slot, original, target);
          printf ("Resolution count raised to %u (offset 70) before switch.\n"
                  "Active slot %u was reselected after the write.\n",
                  target_count, slot);
        }
      else
        printf ("Count already %u; no count write performed.\n",
                target_count);
    }
  if (set_count)
    {
      if (original[74] != 100 || original[75] != 100)
        fail ("State outside inspected DPI layout; nothing written.");
      memcpy (target, original, BLOCK);
      target[70] = (uint8_t)target_count;
      if (memcmp (original, target, BLOCK) != 0)
        {
          commit_profile (fd, profile, slot, original, target);
          printf ("Resolution count set to %u (offset 70).\n"
                  "Active slot %u was reselected after the write.\n",
                  target_count, slot);
        }
      else
        {
          printf ("Count already %u; no count write performed.\n",
                  target_count);
        }
    }
  if (slot_cmd || slot_count)
    {
      if (target_slot == slot)
        printf ("Slot %u already active; no selection write performed.\n",
                slot);
      else
        {
          uint8_t check_profile, check_slot;
          if (send_command (fd, 0x04, profile, (uint8_t)target_slot)
              || current (fd, &check_profile, &check_slot)
              || check_profile != profile || check_slot != target_slot)
            fail ("Slot selection unverified; firmware may have rejected it "
                  "(e.g. count too low).");
          unsigned estimate;
          if (estimate_slot_dpi (original, target_slot, &estimate))
            printf ("Active slot changed to %u (wire index). X raw=0x%02x "
                    "(~%u CPI estimate), Y raw=0x%02x.\n",
                    target_slot, raw_dpi (original, target_slot, false),
                    estimate, raw_dpi (original, target_slot, true));
          else
            printf ("Active slot changed to %u (wire index). X raw=0x%02x "
                    "(estimate unavailable), Y raw=0x%02x.\n",
                    target_slot, raw_dpi (original, target_slot, false),
                    raw_dpi (original, target_slot, true));
        }
      if (slot_count && target_slot < original[70])
        {
          if (original[74] != 100 || original[75] != 100)
            fail ("State outside inspected DPI layout; nothing written.");
          target_count = target_slot;
          memcpy (target, original, BLOCK);
          target[70] = (uint8_t)target_count;
          if (memcmp (original, target, BLOCK) != 0)
            {
              commit_profile (fd, profile, target_slot, original, target);
              printf ("Resolution count lowered to %u (offset 70) after "
                      "switch.\n",
                      target_count);
            }
          else
            printf ("Count already %u; no count write performed.\n",
                    target_count);
        }
      print_state (fd, true, -1);
      close (fd);
      return 0;
    }
  if (set_rate)
    {
      uint8_t raw = rate_to_raw (target_hz), current_raw;
      uint8_t global0[BLOCK];
      if (get_rate (fd, profile, &current_raw))
        fail ("Cannot query current rate; no write attempted.");
      if (raw == current_raw)
        {
          printf ("Rate already %u Hz; no write performed.\n", target_hz);
          close (fd);
          return 0;
        }
      if (snapshot_global (fd, global0))
        fail ("Cannot snapshot global config; no write attempted.");
      if (send_command (fd, 0x03, profile, raw))
        fail ("Rate set command failed; no change confirmed.");
      if (get_rate (fd, profile, &current_raw) || current_raw != raw)
        fail ("Rate readback mismatch after write; state uncertain.");
      if (restore_global_if_changed (fd, global0, "rate write"))
        fail ("GLOBAL RESTORE FAILED; profile 0 may remain corrupted.");
      printf (
          "Report rate set to %u Hz (raw 0x%02x), profile %u.\n"
          "Applied via command 03; no configuration block was rewritten.\n",
          target_hz, raw, profile);
      print_state (fd, true, -1);
      close (fd);
      return 0;
    }
  if (flip_wheel)
    {
      uint8_t buttons[BLOCK], changed[BLOCK], global0[BLOCK];
      fprintf (stderr, "WARNING: firmware bug may corrupt the global sensor "
                       "config when the\n"
                       "button block is written. A runtime snapshot of "
                       "profile 0 is restored\n"
                       "automatically if corruption is detected.\n");
      /* Writing the button block (command 0d) has been observed to zero parts
       * of the global profile-0 configuration (sensor init fields) on this
       * firmware family. Snapshot profile 0 first, then detect and repair the
       * corruption after the write. No offsets or values are hardcoded: the
       * snapshot is read at runtime, so it also holds for other mice. */
      if (snapshot_global (fd, global0))
        fail ("Cannot snapshot global config; no write attempted.");
      if (read_buttons (fd, profile, buttons))
        fail ("Cannot read button config; no write attempted.");
      if (!scroll_is_valid (buttons))
        fail ("Scroll records outside inspected layout; nothing written.");
      bool natural = !scroll_is_natural (buttons);
      memcpy (changed, buttons, BLOCK);
      scroll_set_direction (changed, natural);
      for (int i = 0; i < BLOCK; i++)
        if (changed[i] != buttons[i] && i != SCROLL_REC14 + 2
            && i != SCROLL_REC15 + 2)
          fail ("Scroll edit would touch unrelated bytes; nothing written.");
      fprintf (stderr, "Flipping wheel direction (button block, command 0d); "
                       "all unrelated fields preserved.\n");
      uint8_t recheck[BLOCK];
      if (read_buttons (fd, profile, recheck)
          || memcmp (recheck, buttons, BLOCK))
        fail ("Button config changed during preparation; nothing written.");
      if (write_buttons (fd, profile, changed))
        fail ("WRITE FAILED; state may be partial. Do not retry blindly.");
      if (read_buttons (fd, profile, recheck)
          || memcmp (recheck, changed, BLOCK))
        fail (
            "READBACK FAILED; no activation or automatic rollback attempted.");
      if (restore_global_if_changed (fd, global0, "button write"))
        fail ("GLOBAL RESTORE FAILED; profile 0 may remain corrupted.");
      printf ("Wheel direction flipped: was %s, now %s (records 14/15: "
              "up/down events).\n",
              natural ? "normal" : "natural", natural ? "natural" : "normal");
      print_state (fd, true, -1);
      close (fd);
      return 0;
    }
  if (snapshot)
    {
      uint8_t snap[SNAP_SIZE];
      if (read_snapshot (fd, snap))
        fail ("Cannot read full device snapshot.");
      save_snapshot (snap, argv[2]);
      printf ("Saved snapshot of all %d profiles (config + buttons) to %s\n",
              PROFILES, argv[2]);
      close (fd);
      return 0;
    }
  if (restore)
    {
      uint8_t snap[SNAP_SIZE], blocks[PROFILES][2][BLOCK];
      load_snapshot (argv[2], snap);
      unpack_snapshot (snap, (uint8_t *)blocks);
      uint8_t cur[BLOCK];
      /* Restore profile 0 global config first (sensor init fields). */
      if (read_profile (fd, 0, cur) == 0
          && memcmp (cur, blocks[0][0], BLOCK) != 0)
        write_profile (fd, 0, blocks[0][0]);
      for (unsigned p = 1; p < PROFILES; p++)
        if (read_profile (fd, p, cur) == 0
            && memcmp (cur, blocks[p][0], BLOCK) != 0)
          write_profile (fd, p, blocks[p][0]);
      for (unsigned p = 0; p < PROFILES; p++)
        {
          if (read_buttons (fd, p, cur) == 0
              && memcmp (cur, blocks[p][1], BLOCK) != 0)
            write_buttons (fd, p, blocks[p][1]);
        }
      printf ("Restored full snapshot from %s\n", argv[2]);
      close (fd);
      return 0;
    }
  if (led_color)
    {
      uint8_t color_profile = led_color_dpi ? profile : 0;
      uint8_t color_original[BLOCK], color_target[BLOCK];
      if (led_color_dpi && profile == 0)
        fail ("DPI-stage colors require active profile 1..5; no write "
              "attempted.");
      if (read_profile (fd, color_profile, color_original))
        fail ("Cannot read color configuration; no write attempted.");
      memcpy (color_target, color_original, BLOCK);
      if (led_color_fixed)
        edit_global_fixed_color (color_target, led_colors[0]);
      else if (led_color_palette)
        edit_global_palette (color_target, led_colors);
      else if (!edit_dpi_color (color_target, led_color_slot, led_colors[0]))
        fail ("DPI slot must be 1..8; no write attempted.");
      if (!memcmp (color_original, color_target, BLOCK))
        {
          puts ("Color already set; no configuration write performed.");
          print_state (fd, true, led_color_dpi ? -1 : 0);
          close (fd);
          return 0;
        }
      commit_profile_for_active (fd, color_profile, profile, slot,
                                 color_original, color_target);
      if (led_color_fixed)
        printf ("Global fixed-color table set to #%02x%02x%02x; profile 0 "
                "indicator-enable bytes and active profile preserved.\n",
                led_colors[0][0], led_colors[0][1], led_colors[0][2]);
      else if (led_color_palette)
        puts ("Global eight-color palette written to profile 0; active profile "
             "preserved.");
      else
        printf ("DPI slot %u color set to #%02x%02x%02x in profile %u.\n",
                led_color_slot, led_colors[0][0], led_colors[0][1],
                led_colors[0][2], profile);
      puts ("Configuration readback verified; visible LED effect remains "
           "hardware-unverified.");
      print_state (fd, true, led_color_dpi ? -1 : 0);
      close (fd);
      return 0;
    }
  if (led_mode)
    {
      memcpy (target, original, BLOCK);
      target[ILLUM_MODE] = (uint8_t)led_mode_value;
      target[ILLUM_PARAM_A] = led_byte_a;
      target[ILLUM_PARAM_B] = led_byte_b;
      if (!memcmp (original, target, BLOCK))
        {
          puts ("Already set; no configuration write performed.");
          print_state (fd, true, -1);
          close (fd);
          return 0;
        }
      commit_profile (fd, profile, slot, original, target);
      printf ("LED mode %u (%s) written to profile %u; verified block bytes "
              "71..73=%u/%u/%u. Physical effect is not hardware-verified.\n",
              led_mode_value, illum_mode_name ((uint8_t)led_mode_value),
              profile, target[ILLUM_MODE], target[ILLUM_PARAM_A],
              target[ILLUM_PARAM_B]);
      print_state (fd, true, -1);
      close (fd);
      return 0;
    }
  if (original[70] < slot || original[70] > 8
      || !(original[100] & (1u << (slot - 1)))
      || (original[82] & (1u << (slot - 1))) || original[74] != 100
      || original[75] != 100)
    fail ("State outside inspected DPI layout; nothing written.");
  memcpy (target, original, BLOCK);
  edit_dpi (target, slot, dpi);
  if (!memcmp (original, target, BLOCK))
    {
      puts ("Already set; no configuration write, or activation needed.");
      print_state (fd, true, -1);
      close (fd);
      return 0;
    }
  commit_profile (fd, profile, slot, original, target);
  unsigned estimate;
  if (estimate_slot_dpi (target, slot, &estimate))
    printf ("Verified block readback: requested=%u, X code=0x%02x, "
            "Sensor=3327 estimate=%u CPI.\n",
            dpi, raw_dpi (target, slot, false), estimate);
  else
    printf ("Verified block readback: requested=%u, X code=0x%02x; "
            "CPI estimate unavailable.\n",
            dpi, raw_dpi (target, slot, false));
  printf ("Physical sensitivity and persistence across power cycles are not "
          "measured.\n");
  print_state (fd, true, -1);
  close (fd);
  return 0;
}
