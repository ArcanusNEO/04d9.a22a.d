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
 * X is assumed to drive both axes. Sensor is inferred to be PixArt PAW33xx
 * (PAW3333 family) over SPI, CPI = raw * 100 with a 6-bit resolution register:
 * raw >= 64 wraps modulo 64, so the effective range is raw 0..63 (0..6300 CPI).
 */
enum { BLOCK = 128, BACKUP = 144, RES_BITS = 6, RAW_MASK = 63, MAX_DPI = 6300 };
static const uint8_t descriptor[] = {
    0x06,0x00,0xff,0x0a,0x00,0xff,0xa1,0x01,0x15,0x00,0x26,0xff,0x00,0x09,0x20,0x75,
    0x08,0x95,0x40,0x81,0x02,0x09,0x21,0x91,0x02,0x09,0x22,0x95,0x08,0xb1,0x02,0xc0
};

static void fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static void packet(uint8_t out[9], uint8_t command, uint8_t a, uint8_t b)
{
    memset(out, 0, 9);
    out[1] = command;
    out[2] = a;
    out[3] = b;
    out[8] = (uint8_t)(255 - command - a - b);
}

static int send_command(int fd, uint8_t command, uint8_t a, uint8_t b)
{
    uint8_t out[9];
    packet(out, command, a, b);
    return ioctl(fd, HIDIOCSFEATURE(sizeof(out)), out) == 9 ? 0 : -1;
}

static int get_feature(int fd, uint8_t reply[9])
{
    memset(reply, 0, 9);
    return ioctl(fd, HIDIOCGFEATURE(9), reply) == 9 ? 0 : -1;
}

static int query(int fd, uint8_t command, uint8_t profile, uint8_t reply[9])
{
    if (send_command(fd, command, profile, 0) || get_feature(fd, reply))
        return -1;
    return reply[0] == 0 && reply[1] == command ? 0 : -1;
}

static int current(int fd, uint8_t *profile, uint8_t *slot)
{
    uint8_t reply[9];
    if (query(fd, 0x82, 0, reply) || reply[2] >= 6)
        return -1;
    *profile = reply[2];
    if (query(fd, 0x84, *profile, reply) || reply[2] != *profile ||
        reply[3] < 1 || reply[3] > 8)
        return -1;
    *slot = reply[3];
    return 0;
}

static const unsigned rate_hz[] = { 0, 1000, 500, 0, 250, 0, 0, 0, 125 };

static unsigned raw_to_rate_hz(uint8_t raw)
{
    if (raw < sizeof(rate_hz) / sizeof(rate_hz[0]))
        return rate_hz[raw];
    return 0;
}

static uint8_t rate_to_raw(unsigned hz)
{
    switch (hz) {
    case 1000: return 0x01;
    case 500:  return 0x02;
    case 250:  return 0x04;
    case 125:  return 0x08;
    default:   return 0;
    }
}

/* Query the active report rate code for a profile. Returns the raw code or 0. */
static int get_rate(int fd, uint8_t profile, uint8_t *raw)
{
    uint8_t reply[9];
    if (query(fd, 0x83, profile, reply) || reply[2] != profile)
        return -1;
    *raw = reply[3];
    return 0;
}

/* Read a 128-byte block. cmd is the read command (0x8c profile, 0x8d buttons). */
static int read_block(int fd, uint8_t cmd, uint8_t profile, uint8_t data[BLOCK])
{
    uint8_t reply[9], chunk[65];
    struct pollfd p = { .fd = fd, .events = POLLIN };
    /* Do not discard unexplained reports and risk interpreting stale data. */
    if (poll(&p, 1, 0) != 0 || query(fd, cmd, profile, reply) ||
        reply[2] != profile || reply[3] != BLOCK)
        return -1;
    for (int i = 0; i < 2; i++) {
        if (poll(&p, 1, 1500) != 1 || !(p.revents & POLLIN) ||
            (p.revents & (POLLERR | POLLHUP | POLLNVAL)) ||
            read(fd, chunk, sizeof(chunk)) != 64)
            return -1;
        memcpy(data + i * 64, chunk, 64);
    }
    return 0;
}

static int read_profile(int fd, uint8_t profile, uint8_t data[BLOCK])
{
    return read_block(fd, 0x8c, profile, data);
}

static int read_buttons(int fd, uint8_t profile, uint8_t data[BLOCK])
{
    return read_block(fd, 0x8d, profile, data);
}

static int ready(int fd, uint8_t cmd, uint8_t profile, uint8_t remaining)
{
    uint8_t reply[9];
    const struct timespec delay = { .tv_nsec = 1000000 };
    for (int i = 0; i < 50; i++) {
        if (get_feature(fd, reply) || reply[0] || reply[1] != cmd ||
            reply[2] != profile)
            return -1;
        if (reply[3] == remaining)
            return 0;
        nanosleep(&delay, NULL);
    }
    return -1;
}

static int write_block(int fd, uint8_t cmd, uint8_t profile, const uint8_t data[BLOCK])
{
    uint8_t out[65] = {0};
    if (send_command(fd, cmd, profile, BLOCK))
        return -1;
    /* The reference protocol warns that unsynchronized writes can soft-brick. */
    for (int i = 0; i < 2; i++) {
        if (ready(fd, cmd, profile, (uint8_t)(BLOCK - i * 64)))
            return -1;
        memcpy(out + 1, data + i * 64, 64);
        if (write(fd, out, sizeof(out)) != (ssize_t)sizeof(out))
            return -1;
    }
    return ready(fd, cmd, profile, 0);
}

static int write_profile(int fd, uint8_t profile, const uint8_t data[BLOCK])
{
    return write_block(fd, 0x0c, profile, data);
}

static int write_buttons(int fd, uint8_t profile, const uint8_t data[BLOCK])
{
    return write_block(fd, 0x0d, profile, data);
}

static unsigned raw_dpi(const uint8_t data[BLOCK], unsigned slot, bool y)
{
    return data[(y ? 92 : 84) + slot - 1] & RAW_MASK;
}

static void edit_dpi(uint8_t data[BLOCK], unsigned slot, unsigned dpi)
{
    data[84 + slot - 1] = (uint8_t)(dpi / 100);
}

/* Scroll records occupy button-config bytes 56..63: record 14 (scroll up) and
 * record 15 (scroll down). Record layout: type(1) pad event pad. type 0x04 is
 * scroll; event 0x01 = up, 0x02 = down (reference decoding). */
enum { SCROLL_REC14 = 56, SCROLL_REC15 = 60, SCROLL_TYPE = 0x04 };
enum { SCROLL_UP = 0x01, SCROLL_DOWN = 0x02 };

static void scroll_set_direction(uint8_t data[BLOCK], bool inverted)
{
    /* Normal: rec14=up, rec15=down. Inverted: rec14=down, rec15=up. */
    data[SCROLL_REC14 + 2] = inverted ? SCROLL_DOWN : SCROLL_UP;
    data[SCROLL_REC15 + 2] = inverted ? SCROLL_UP : SCROLL_DOWN;
}

/* Returns true if the two scroll records are type 0x04 and currently inverted. */
static bool scroll_is_valid(const uint8_t data[BLOCK])
{
    bool rec14 = data[SCROLL_REC14] == SCROLL_TYPE;
    bool rec15 = data[SCROLL_REC15] == SCROLL_TYPE;
    bool up14 = data[SCROLL_REC14 + 2] == SCROLL_UP;
    bool dn14 = data[SCROLL_REC14 + 2] == SCROLL_DOWN;
    bool up15 = data[SCROLL_REC15 + 2] == SCROLL_UP;
    bool dn15 = data[SCROLL_REC15 + 2] == SCROLL_DOWN;
    bool normal = rec14 && rec15 && up14 && dn15;
    bool inverted = rec14 && rec15 && dn14 && up15;
    return normal || inverted;
}

static bool scroll_is_inverted(const uint8_t data[BLOCK])
{
    return data[SCROLL_REC14] == SCROLL_TYPE &&
           data[SCROLL_REC14 + 2] == SCROLL_DOWN &&
           data[SCROLL_REC15] == SCROLL_TYPE &&
           data[SCROLL_REC15 + 2] == SCROLL_UP;
}

static uint32_t checksum(const uint8_t *data, size_t size)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}

static void pack_backup(uint8_t out[BACKUP], uint8_t profile, uint8_t slot,
                        const uint8_t data[BLOCK])
{
    memcpy(out, "A22ADPI1", 8);
    out[8] = 0x01;
    out[9] = 0x01; /* bcdDevice, little endian */
    out[10] = profile;
    out[11] = slot;
    memcpy(out + 12, data, BLOCK);
    uint32_t hash = checksum(out, BACKUP - 4);
    for (unsigned i = 0; i < 4; i++)
        out[BACKUP - 4 + i] = (uint8_t)(hash >> (8 * i));
}

static bool valid_backup(const uint8_t data[BACKUP])
{
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; i++)
        stored |= (uint32_t)data[BACKUP - 4 + i] << (8 * i);
    return !memcmp(data, "A22ADPI1", 8) && data[8] == 1 && data[9] == 1 &&
           data[10] < 6 && data[11] >= 1 && data[11] <= 8 &&
           stored == checksum(data, BACKUP - 4);
}

static void save_backup(uint8_t profile, uint8_t slot, const uint8_t data[BLOCK])
{
    char path[] = "/tmp/a22a-dpi-backup-XXXXXX";
    uint8_t out[BACKUP];
    pack_backup(out, profile, slot, data);
    int fd = mkstemp(path);
    if (fd < 0)
        fail("Cannot create backup; device not written.");
    size_t pos = 0;
    while (pos < sizeof(out)) {
        ssize_t n = write(fd, out + pos, sizeof(out) - pos);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            fail("Cannot complete backup; device not written.");
        pos += (size_t)n;
    }
    if (fsync(fd) || close(fd))
        fail("Cannot flush backup; device not written.");
    if (printf("Backup (0600): %s\n", path) < 0 || fflush(stdout))
        fail("Cannot report backup path; device not written.");
}

static void load_backup(const char *path, uint8_t profile, uint8_t slot,
                        uint8_t data[BLOCK])
{
    uint8_t input[BACKUP + 1];
    FILE *file = fopen(path, "rb");
    if (!file)
        fail("Cannot open backup.");
    size_t n = fread(input, 1, sizeof(input), file);
    bool ok = !ferror(file) && n == BACKUP && valid_backup(input);
    fclose(file);
    if (!ok || input[10] != profile || input[11] != slot)
        fail("Backup invalid, or active profile/slot differs. Nothing written.");
    memcpy(data, input + 12, BLOCK);
}

static int open_mouse(void)
{
    glob_t paths = {0};
    int selected = -1;
    if (glob("/sys/bus/hid/devices/0003:04D9:A22A.*/hidraw/hidraw*", 0, NULL, &paths))
        fail("No A22A hidraw nodes found.");
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        const char *name = strrchr(paths.gl_pathv[i], '/') + 1;
        char path[256];
        snprintf(path, sizeof(path), "/dev/%s", name);
        int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        struct hidraw_devinfo info = {0};
        int descriptor_size = 0;
        struct hidraw_report_descriptor desc = { .size = sizeof(descriptor) };
        if (ioctl(fd, HIDIOCGRAWINFO, &info) || info.bustype != BUS_USB ||
            (uint16_t)info.vendor != 0x04d9 || (uint16_t)info.product != 0xa22a ||
            ioctl(fd, HIDIOCGRDESCSIZE, &descriptor_size) ||
            descriptor_size != (int)sizeof(descriptor) || ioctl(fd, HIDIOCGRDESC, &desc) ||
            memcmp(desc.value, descriptor, sizeof(descriptor))) {
            close(fd);
            continue;
        }
        snprintf(path, sizeof(path), "/sys/class/hidraw/%s/device/../../bcdDevice", name);
        FILE *revision = fopen(path, "r");
        unsigned value = 0;
        if (revision) {
            if (fscanf(revision, "%x", &value) != 1)
                value = 0;
            fclose(revision);
        }
        if (value != 0x0101)
            fail("Unverified bcdDevice revision; refusing access.");
        if (selected >= 0)
            fail("Multiple matching mice; connect only one A22A.");
        selected = fd;
    }
    globfree(&paths);
    if (selected < 0)
        fail("Cannot open matching configuration interface; try sudo.");
    if (flock(selected, LOCK_EX | LOCK_NB))
        fail("Another cooperating process holds this device lock.");
    return selected;
}

/* Full guarded configuration-table write: backup, preflight re-read, write,
 * readback, reselect current slot, final readback. `slot` is the wire slot to
 * reselect after the write; `target` is the intended full 128-byte block. */
static void commit_profile(int fd, uint8_t profile, uint8_t slot,
                           const uint8_t original[BLOCK], const uint8_t target[BLOCK])
{
    uint8_t verify[BLOCK];
    save_backup(profile, slot, original);
    fprintf(stderr, "Preparing configuration write; all unrelated fields preserved.\n");
    uint8_t check_profile, check_slot;
    if (current(fd, &check_profile, &check_slot) || check_profile != profile ||
        check_slot != slot || read_profile(fd, profile, verify) || memcmp(verify, original, BLOCK))
        fail("State changed during preparation; nothing written.");
    if (write_profile(fd, profile, target))
        fail("WRITE FAILED; state may be partial. Backup retained. Do not retry blindly.");
    if (read_profile(fd, profile, verify) || memcmp(verify, target, BLOCK))
        fail("READBACK FAILED; backup retained. No activation or automatic rollback attempted.");
    if (send_command(fd, 0x04, profile, slot) || current(fd, &check_profile, &check_slot) ||
        check_profile != profile || check_slot != slot)
        fail("Configuration written, but activation unverified. Backup retained.");
    if (read_profile(fd, profile, verify) || memcmp(verify, target, BLOCK))
        fail("Post-activation readback differs; backup retained. Inspect before further writes.");
}

static int self_test(void)
{
    uint8_t data[BLOCK], changed[BLOCK], p[9], backup[BACKUP];
    packet(p, 0x8c, 1, 0);
    if (memcmp(p, (uint8_t[]){0,0x8c,1,0,0,0,0,0,0x72}, 9))
        return 1;
    packet(p, 0x0c, 1, BLOCK);
    if (p[8] != 0x72)
        return 1;
    for (unsigned i = 0; i < BLOCK; i++)
        data[i] = (uint8_t)(i * 17);
    for (unsigned slot = 1; slot <= 8; slot++) {
        for (unsigned dpi = 100; dpi <= MAX_DPI; dpi += 100) {
            memcpy(changed, data, BLOCK);
            edit_dpi(changed, slot, dpi);
            if (raw_dpi(changed, slot, false) * 100 != dpi ||
                raw_dpi(changed, slot, true) != raw_dpi(data, slot, true))
                return 1;
            for (unsigned i = 0; i < BLOCK; i++) {
                if (i != 84 + slot - 1 && changed[i] != data[i])
                    return 1;
            }
        }
    }
    memcpy(changed, data, BLOCK);
    edit_dpi(changed, 1, MAX_DPI);
    changed[84] = 64;
    if (raw_dpi(changed, 1, false) != 0)
        return 1;
    changed[84] = 80;
    if (raw_dpi(changed, 1, false) != 16)
        return 1;
    pack_backup(backup, 1, 1, data);
    if (!valid_backup(backup) || memcmp(backup + 12, data, BLOCK))
        return 1;
    for (unsigned i = 0; i < BACKUP; i++) {
        backup[i] ^= 1;
        if (valid_backup(backup))
            return 1;
        backup[i] ^= 1;
    }
    puts("PASS: packets, all slots/6-bit encodings, mod-64 wrap, unrelated-byte preservation, backup corruption.");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--self-test"))
        return self_test();
    bool show = argc == 2 && !strcmp(argv[1], "show");
    bool plan = argc == 3 && !strcmp(argv[1], "plan");
    bool set = argc == 4 && !strcmp(argv[1], "set");
    bool restore = argc == 4 && !strcmp(argv[1], "restore");
    bool switch_slot = argc == 4 && !strcmp(argv[1], "switch");
    bool set_count = argc == 4 && !strcmp(argv[1], "count");
    bool set_rate = argc == 4 && !strcmp(argv[1], "rate");
    bool flip_wheel = argc == 3 && !strcmp(argv[1], "wheel");
    if (!show && !plan && !(set || restore || switch_slot || set_count || set_rate || flip_wheel) &&
        (!(set || restore || switch_slot || set_count || set_rate) || strcmp(argv[3], "--allow-persistent-write")) &&
        (!flip_wheel || strcmp(argv[2], "--allow-persistent-write"))) {
        fprintf(stderr, "Usage:\n  %s show\n  %s --self-test\n  %s plan DPI\n"
                "  %s set DPI --allow-persistent-write\n"
                "  %s switch SLOT --allow-persistent-write\n"
                "  %s count N --allow-persistent-write\n"
                "  %s rate HZ --allow-persistent-write\n"
                "  %s wheel --allow-persistent-write\n"
                "  %s restore BACKUP --allow-persistent-write\n"
                "Experimental shared-X DPI, step 100, sensor 6-bit (100..6300).\n"
                "Rate options: 125, 250, 500, 1000.\n"
                "Writes may persist. Close vendor software; do not unplug during writes.\n",
                argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    unsigned dpi = 0;
    if (set || plan) {
        char *end;
        errno = 0;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (errno || end == argv[2] || *end || value < 100 || value > MAX_DPI || value % 100)
            fail("DPI must be 100..6300 in steps of 100 (raw 6-bit sensor); device not opened.");
        dpi = (unsigned)value;
    }
    unsigned target_slot = 0;
    if (switch_slot) {
        char *end;
        errno = 0;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (errno || end == argv[2] || *end || value < 1 || value > 8)
            fail("Slot must be 1..8; device not opened.");
        target_slot = (unsigned)value;
    }
    unsigned target_count = 0;
    if (set_count) {
        char *end;
        errno = 0;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (errno || end == argv[2] || *end || value < 1 || value > 8)
            fail("Count must be 1..8; device not opened.");
        target_count = (unsigned)value;
    }
    unsigned target_hz = 0;
    if (set_rate) {
        char *end;
        errno = 0;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (errno || end == argv[2] || *end || !rate_to_raw((unsigned)value))
            fail("Rate must be 125, 250, 500 or 1000; device not opened.");
        target_hz = (unsigned)value;
    }
    int fd = open_mouse();
    uint8_t profile, slot, original[BLOCK], target[BLOCK], verify[BLOCK];
    if (current(fd, &profile, &slot) || read_profile(fd, profile, original))
        fail("Cannot read current state; no configuration write attempted.");
    printf("Device 04d9:a22a revision 0101; profile=%u, slot=%u (wire indices)\n", profile, slot);
    printf("Current X raw=%u, estimated shared DPI=%u; Y raw=%u (preserved).\n",
           raw_dpi(original, slot, false), raw_dpi(original, slot, false) * 100,
           raw_dpi(original, slot, true));
    printf("Candidate count=%u, enabled mask=0x%02x, scale X/Y=%u/%u\n",
           original[70], original[100], original[74], original[75]);
    uint8_t rate_raw;
    if (get_rate(fd, profile, &rate_raw) == 0)
        printf("Report rate raw=0x%02x (~%u Hz); enabled-rates field=0x%02x\n",
               rate_raw, raw_to_rate_hz(rate_raw), original[64]);
    else
        printf("Report rate query failed; enabled-rates field=0x%02x\n", original[64]);
    uint8_t buttons[BLOCK];
    if (read_buttons(fd, profile, buttons) == 0 && scroll_is_valid(buttons))
        printf("Wheel direction: %s\n", scroll_is_inverted(buttons) ? "inverted" : "normal");
    else
        printf("Wheel direction: unknown (scroll records unrecognized)\n");
    printf("Sensor inferred PAW33xx 6-bit; raw>=64 wraps mod 64 (0..63 => 0..6300 CPI).\n"
           "Unverified high masks X/Y=0x%02x/0x%02x; displayed DPI uses low 6 bits.\n",
           original[82], original[83]);
    if (show) {
        for (unsigned s = 1; s <= 8; s++)
            printf("slot %u: X=%u (~%u DPI), Y=%u, enabled-by-count-and-mask=%s\n",
                   s, raw_dpi(original,s,false), raw_dpi(original,s,false)*100,
                   raw_dpi(original,s,true),
                   s <= original[70] && (original[100] & (1u << (s-1))) ? "yes" : "no");
        close(fd);
        return 0;
    }
    if (switch_slot) {
        if (target_slot == slot) {
            printf("Slot %u already active; no configuration write performed.\n", slot);
            close(fd);
            return 0;
        }
        uint8_t check_profile, check_slot;
        if (send_command(fd, 0x04, profile, (uint8_t)target_slot) ||
            current(fd, &check_profile, &check_slot) ||
            check_profile != profile || check_slot != target_slot)
            fail("Slot selection unverified; firmware may have rejected it (e.g. count too low).");
        printf("Active slot changed to %u (wire index). X low=%u (~%u DPI), Y low=%u.\n"
               "No configuration bytes were rewritten; only the active slot was selected.\n",
               target_slot, raw_dpi(original, target_slot, false),
               raw_dpi(original, target_slot, false) * 100,
               raw_dpi(original, target_slot, true));
        close(fd);
        return 0;
    }
    if (set_count) {
        if (original[74] != 100 || original[75] != 100)
            fail("State outside inspected DPI layout; nothing written.");
        memcpy(target, original, BLOCK);
        target[70] = (uint8_t)target_count;
        if (memcmp(original, target, BLOCK) == 0) {
            printf("Count already %u; no configuration write performed.\n", target_count);
            close(fd);
            return 0;
        }
        commit_profile(fd, profile, slot, original, target);
        printf("Resolution count set to %u (offset 70).\n"
               "Active slot %u was reselected after the write.\n", target_count, slot);
        close(fd);
        return 0;
    }
    if (set_rate) {
        uint8_t raw = rate_to_raw(target_hz), current_raw;
        if (get_rate(fd, profile, &current_raw))
            fail("Cannot query current rate; no write attempted.");
        if (raw == current_raw) {
            printf("Rate already %u Hz; no write performed.\n", target_hz);
            close(fd);
            return 0;
        }
        if (send_command(fd, 0x03, profile, raw))
            fail("Rate set command failed; no change confirmed.");
        if (get_rate(fd, profile, &current_raw) || current_raw != raw)
            fail("Rate readback mismatch after write; state uncertain.");
        printf("Report rate set to %u Hz (raw 0x%02x), profile %u.\n"
               "Applied via command 03; no configuration block was rewritten.\n",
               target_hz, raw, profile);
        close(fd);
        return 0;
    }
    if (flip_wheel) {
        uint8_t buttons[BLOCK], changed[BLOCK];
        if (read_buttons(fd, profile, buttons))
            fail("Cannot read button config; no write attempted.");
        if (!scroll_is_valid(buttons))
            fail("Scroll records outside inspected layout; nothing written.");
        bool inverted = !scroll_is_inverted(buttons);
        memcpy(changed, buttons, BLOCK);
        scroll_set_direction(changed, inverted);
        for (int i = 0; i < BLOCK; i++)
            if (changed[i] != buttons[i] && i != SCROLL_REC14 + 2 && i != SCROLL_REC15 + 2)
                fail("Scroll edit would touch unrelated bytes; nothing written.");
        save_backup(profile, slot, buttons);
        fprintf(stderr, "Flipping wheel direction (button block, command 0d); all unrelated fields preserved.\n");
        uint8_t recheck[BLOCK];
        if (read_buttons(fd, profile, recheck) || memcmp(recheck, buttons, BLOCK))
            fail("Button config changed during preparation; nothing written.");
        if (write_buttons(fd, profile, changed))
            fail("WRITE FAILED; state may be partial. Backup retained. Do not retry blindly.");
        if (read_buttons(fd, profile, recheck) || memcmp(recheck, changed, BLOCK))
            fail("READBACK FAILED; backup retained. No activation or automatic rollback attempted.");
        printf("Wheel direction flipped: was %s, now %s (records 14/15: up/down events).\n"
               "No activation command is sent; the change takes effect via the button block.\n",
               inverted ? "normal" : "inverted", inverted ? "inverted" : "normal");
        close(fd);
        return 0;
    }
    if (original[70] < slot || original[70] > 8 ||
        !(original[100] & (1u << (slot-1))) || (original[82] & (1u << (slot-1))) ||
        original[74] != 100 || original[75] != 100)
        fail("State outside inspected DPI layout; nothing written.");
    memcpy(target, original, BLOCK);
    if (set || plan)
        edit_dpi(target, slot, dpi);
    else {
        load_backup(argv[2], profile, slot, target);
        memcpy(verify, original, BLOCK);
        edit_dpi(verify, slot, raw_dpi(target, slot, false) * 100);
        if (memcmp(verify, target, BLOCK))
            fail("Backup differs beyond current X DPI; refusing unrelated changes.");
    }
    if (plan) {
        for (unsigned i = 0; i < BLOCK; i++)
            if (original[i] != target[i])
                printf("Planned offset %u: %02x -> %02x\n", i, original[i], target[i]);
        puts("Plan only: no backup, configuration write, or activation performed.");
        close(fd);
        return 0;
    }
    if (!memcmp(original, target, BLOCK)) {
        puts("Already set; no backup, configuration write, or activation needed.");
        close(fd);
        return 0;
    }
    commit_profile(fd, profile, slot, original, target);
    printf("Verified configuration readback, estimated shared DPI=%u.\n"
           "Physical sensitivity and persistence across power cycles are not measured.\n",
           raw_dpi(target, slot, false) * 100);
    close(fd);
    return 0;
}
