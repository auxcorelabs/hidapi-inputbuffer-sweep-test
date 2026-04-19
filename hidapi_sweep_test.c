/* hidapi_sweep_test.c
 *
 * Sweep test for hid_set_num_input_buffers() — added in upstream
 * libusb/hidapi PR #787 (https://github.com/libusb/hidapi/pull/787).
 *
 * Calls hid_set_num_input_buffers(dev, i) for every value in a
 * user-specified range on a real HID device and tabulates the return
 * codes to expose the valid-range boundaries. Useful for verifying
 * behavior across backends (macOS IOKit, Linux hidraw, Linux libusb,
 * Windows HidD_SetNumInputBuffers, NetBSD uhid).
 *
 * Usage:
 *   sweep_test <start_dec> <end_dec> [--vid <hex> --pid <hex>]
 *
 *   start_dec   Sweep start value (decimal integer, may be negative)
 *   end_dec     Sweep end value (decimal integer, must be >= start)
 *   --vid <hex> (optional) USB vendor id in hex, no 0x prefix
 *   --pid <hex> (optional) USB product id in hex, no 0x prefix
 *
 *   If --vid and --pid are BOTH given, that specific device is opened.
 *   If NEITHER is given, the first enumerated HID device is opened.
 *   Passing only one of the two is an error.
 *
 *   The device itself does not affect the sweep result — our function's
 *   return value depends on the OS backend, not the device. --vid/--pid
 *   exist only to target a specific device if you want.
 *
 * Examples:
 *   sweep_test 0 1025                                  auto-detect, full sweep
 *   sweep_test 510 515                                 auto-detect, around Windows' 512 edge
 *   sweep_test 0 1025 --vid 28E9 --pid 028A            Beurer PO80, full sweep
 *   sweep_test --vid 28E9 --pid 028A -5 5              PO80, negative edge
 *
 * Build (macOS):
 *   gcc -I /path/to/hidapi -o sweep_test hidapi_sweep_test.c \
 *       -L /path/to/libs -lhidapi -framework IOKit -framework CoreFoundation
 *
 * Build (Linux hidraw):
 *   gcc -I /path/to/hidapi -o sweep_test hidapi_sweep_test.c \
 *       -L /path/to/libs -lhidapi-hidraw
 *
 * Build (Windows MSVC):
 *   cl.exe /nologo /I /path/to/hidapi sweep_test.c hidapi.lib
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <hidapi.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <start_dec> <end_dec> [--vid <hex> --pid <hex>]\n"
        "\n"
        "  start_dec   sweep start value (decimal integer, may be negative)\n"
        "  end_dec     sweep end value (decimal integer, must be >= start)\n"
        "  --vid <hex> USB vendor id in hex, no 0x prefix\n"
        "  --pid <hex> USB product id in hex, no 0x prefix\n"
        "\n"
        "--vid and --pid must be given TOGETHER or both OMITTED.\n"
        "If omitted, the first enumerated HID device is opened.\n"
        "\n"
        "Examples:\n"
        "  %s 0 1025                                full sweep, auto-detect device\n"
        "  %s 510 515 --vid 28E9 --pid 028A         narrow sweep against PO80\n",
        prog, prog, prog);
}

static int parse_hex_u16(const char *s, unsigned short *out)
{
    char *endp;
    unsigned long v = strtoul(s, &endp, 16);
    if (*s == '\0' || *endp != '\0' || v > 0xFFFF)
        return -1;
    *out = (unsigned short)v;
    return 0;
}

static int parse_dec_long(const char *s, long *out)
{
    char *endp;
    long v = strtol(s, &endp, 10);
    if (*s == '\0' || *endp != '\0')
        return -1;
    *out = v;
    return 0;
}

int main(int argc, char **argv)
{
    /* Required for printf("%ls", ...) to convert wide product_string to bytes
     * in the user's locale; otherwise non-ASCII device names would be dropped. */
    setlocale(LC_ALL, "");

    int have_vid = 0, have_pid = 0;
    unsigned short vid = 0, pid = 0;

    /* Collect positional args here after pulling --vid/--pid out. */
    const char *positional[8];
    int npositional = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vid") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --vid requires a value\n");
                usage(argv[0]);
                return 1;
            }
            if (parse_hex_u16(argv[i + 1], &vid) != 0) {
                fprintf(stderr, "Error: bad --vid value: %s (expected hex uint16)\n", argv[i + 1]);
                usage(argv[0]);
                return 1;
            }
            have_vid = 1;
            i++;
        } else if (strcmp(argv[i], "--pid") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --pid requires a value\n");
                usage(argv[0]);
                return 1;
            }
            if (parse_hex_u16(argv[i + 1], &pid) != 0) {
                fprintf(stderr, "Error: bad --pid value: %s (expected hex uint16)\n", argv[i + 1]);
                usage(argv[0]);
                return 1;
            }
            have_pid = 1;
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            if (npositional >= (int)(sizeof(positional) / sizeof(positional[0]))) {
                fprintf(stderr, "Error: too many positional arguments\n");
                usage(argv[0]);
                return 1;
            }
            positional[npositional++] = argv[i];
        }
    }

    /* --vid and --pid must be both present or both absent. */
    if (have_vid != have_pid) {
        fprintf(stderr, "Error: --vid and --pid must be given together "
                        "(got only --%s)\n",
                have_vid ? "vid" : "pid");
        usage(argv[0]);
        return 1;
    }

    if (npositional != 2) {
        fprintf(stderr, "Error: expected exactly 2 positional args "
                        "(start_dec, end_dec), got %d\n", npositional);
        usage(argv[0]);
        return 1;
    }

    long start, end;
    if (parse_dec_long(positional[0], &start) != 0) {
        fprintf(stderr, "Error: bad start_dec value: %s (expected decimal integer)\n",
                positional[0]);
        usage(argv[0]);
        return 1;
    }
    if (parse_dec_long(positional[1], &end) != 0) {
        fprintf(stderr, "Error: bad end_dec value: %s (expected decimal integer)\n",
                positional[1]);
        usage(argv[0]);
        return 1;
    }
    if (start > end) {
        fprintf(stderr, "Error: start (%ld) must be <= end (%ld)\n", start, end);
        return 1;
    }

    long count = end - start + 1;
    if (count <= 0 || count > 1000000L) {
        fprintf(stderr, "Error: range too large (%ld values). Limit is 1,000,000.\n", count);
        return 1;
    }

    if (hid_init() != 0) {
        fprintf(stderr, "hid_init failed\n");
        return 2;
    }

    hid_device *dev = NULL;
    if (have_vid && have_pid) {
        dev = hid_open(vid, pid, NULL);
        if (dev) {
            printf("Opened: vid=0x%04x pid=0x%04x (explicit)\n", vid, pid);
        } else {
            fprintf(stderr, "Could not open device vid=0x%04x pid=0x%04x\n", vid, pid);
        }
    } else {
        struct hid_device_info *list = hid_enumerate(0, 0);
        if (list) {
            dev = hid_open_path(list->path);
            if (dev) {
                /* Narrow-char conversion of product_string (wide) via %ls. Keep the
                 * surrounding stream byte-oriented to avoid glibc's wide/narrow
                 * orientation trap (C99 §7.19.2). */
                printf("Opened: vid=0x%04x pid=0x%04x product=%ls (auto-detected)\n",
                    list->vendor_id, list->product_id,
                    list->product_string ? list->product_string : L"?");
            }
            hid_free_enumeration(list);
        } else {
            fprintf(stderr, "No HID devices found for auto-detect\n");
        }
    }

    if (!dev) {
        hid_exit();
        return 3;
    }

    printf("Sweeping range [%ld, %ld] (%ld values)...\n", start, end, count);

    int *results = (int *)malloc((size_t)count * sizeof(int));
    if (!results) {
        fprintf(stderr, "malloc failed\n");
        hid_close(dev);
        hid_exit();
        return 4;
    }

    for (long i = 0; i < count; i++) {
        results[i] = hid_set_num_input_buffers(dev, (int)(start + i));
    }

    printf("\nResults (compact ranges):\n");
    printf("  value_range             return_value\n");
    printf("  ----------------------  ------------\n");
    long range_start = 0;
    for (long i = 1; i <= count; i++) {
        if (i == count || results[i] != results[range_start]) {
            long lo = start + range_start;
            long hi = start + (i - 1);
            if (lo == hi)
                printf("  %6ld                  %3d\n", lo, results[range_start]);
            else
                printf("  %6ld .. %-6ld        %3d\n", lo, hi, results[range_start]);
            range_start = i;
        }
    }

    free(results);
    hid_close(dev);
    hid_exit();
    return 0;
}
