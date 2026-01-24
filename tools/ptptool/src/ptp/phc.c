#include "platform.h" // for _LINUX

// Linux only code, not for MacOS or Windows
#ifdef _LINUX

// logging replaced by ptp module logging
extern uint8_t ptp_log_level;
#define DBG_LEVEL ptp_log_level
#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...

// Original file header

/**
 * @file phc.c
 * @note Copyright (C) 2011 Richard Cochran <richardcochran@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE

#include <assert.h>   // for assert
#include <errno.h>    // for
#include <fcntl.h>    // for open
#include <inttypes.h> // for PRIu64
#include <math.h>     // for fabs
#include <net/if.h>
#include <signal.h>      // for signal handling
#include <stdbool.h>     // for bool
#include <stdint.h>      // for uintxx_t
#include <stdio.h>       // for printf
#include <stdlib.h>      // for malloc, free
#include <string.h>      // for sprintf
#include <sys/ioctl.h>   // for ioctl
#include <sys/stat.h>    // for struct stat
#include <sys/syscall.h> // for syscall
#include <sys/time.h>    // for struct timeval
#include <sys/timex.h>   // for struct timex
#include <sys/types.h>   //
#include <time.h>        // for struct timespec
#include <unistd.h>      // for close

#include <linux/ethtool.h>
#include <linux/ptp_clock.h>
#include <linux/sockios.h>

#include "phc.h"

/*
 * On 32 bit platforms, the PHC driver's maximum adjustment (type
 * 'int' in units of ppb) can overflow the timex.freq field (type
 * 'long'). So in this case we clamp the maximum to the largest
 * possible adjustment that fits into a 32 bit long.
 */
#define BITS_PER_LONG (sizeof(long) * 8)
#define MAX_PPB_32 32767999 /* 2^31 - 1 / 65.536 */

static int phc_get_caps(clockid_t clkid, struct ptp_clock_caps *caps);

clockid_t phc_open(const char *phc) {
    clockid_t clkid;
    struct timespec ts;
    struct timex tx;
    int fd;

    memset(&tx, 0, sizeof(tx));

    fd = open(phc, O_RDWR);
    if (fd < 0) {
        DBG_PRINTF_ERROR("phc_open: Failed to open %s: %s (errno=%d)\n", phc, strerror(errno), errno);
        return CLOCK_INVALID;
    }

    clkid = FD_TO_CLOCKID(fd);
    /* check if clkid is valid */
    if (clock_gettime(clkid, &ts)) {
        DBG_PRINTF_ERROR("phc_open: clock_gettime failed for %s: %s (errno=%d)\n", phc, strerror(errno), errno);
        close(fd);
        return CLOCK_INVALID;
    }

    // Validates the clockid is functional - Confirms the kernel recognizes this as a valid adjustable clock
    // Tests permission/capability - Verifies the process has CAP_SYS_TIME capability
    // Checks driver support - Ensures the PHC driver properly implements the adjustment interface
    // Returns current clock state - The kernel fills in the timex structure with current adjustment parameters (freq, offset, status, etc.)
    if (clock_adjtime(clkid, &tx)) {
        // EBUSY means another process (e.g., ptp4l) is already controlling the clock
        // This is OK if we only need read access
        if (errno != EBUSY) {
            DBG_PRINTF_ERROR("phc_open: clock_adjtime failed for %s: %s (errno=%d)\n", phc, strerror(errno), errno);
            close(fd);
            return CLOCK_INVALID;
        }
        // EBUSY is acceptable - we can still read the clock
        DBG_PRINTF_WARNING("phc_open: Note: %s is being adjusted by another process (read-only access)\n", phc);
    }

    return clkid;
}

void phc_close(clockid_t clkid) {
    if (clkid == CLOCK_INVALID)
        return;

    close(CLOCKID_TO_FD(clkid));
}

static int phc_get_caps(clockid_t clkid, struct ptp_clock_caps *caps) {
    int fd = CLOCKID_TO_FD(clkid), err;

    err = ioctl(fd, PTP_CLOCK_GETCAPS, caps);
    if (err)
        perror("PTP_CLOCK_GETCAPS");
    return err;
}

int phc_max_adj(clockid_t clkid) {
    int max;
    struct ptp_clock_caps caps;

    if (phc_get_caps(clkid, &caps))
        return 0;

    max = caps.max_adj;

    if (BITS_PER_LONG == 32 && max > MAX_PPB_32)
        max = MAX_PPB_32;

    return max;
}

int phc_number_pins(clockid_t clkid) {
    struct ptp_clock_caps caps;

    if (phc_get_caps(clkid, &caps)) {
        return 0;
    }
    return caps.n_pins;
}

int phc_pin_setfunc(clockid_t clkid, struct ptp_pin_desc *desc) {
    int err = ioctl(CLOCKID_TO_FD(clkid), PTP_PIN_SETFUNC2, desc);
    if (err) {
        DBG_PRINT_ERROR(PTP_PIN_SETFUNC_FAILED "\n");
    }
    return err;
}

int phc_has_pps(clockid_t clkid) {
    struct ptp_clock_caps caps;

    if (phc_get_caps(clkid, &caps))
        return 0;
    return caps.pps;
}

int phc_has_writephase(clockid_t clkid) {
    struct ptp_clock_caps caps;

    if (phc_get_caps(clkid, &caps)) {
        return 0;
    }
    return caps.adjust_phase;
}

int phc_get_pin_index(clockid_t clkid, const char *pin_name) {
    int fd = CLOCKID_TO_FD(clkid), err, i;
    struct ptp_clock_caps caps;
    struct ptp_pin_desc desc;

    err = phc_get_caps(clkid, &caps);
    if (err)
        return -1;

    for (i = 0; i < caps.n_pins; i++) {
        memset(&desc, 0, sizeof(desc));
        desc.index = i;

        err = ioctl(fd, PTP_PIN_GETFUNC, &desc);
        if (err) {
            perror("PTP_PIN_GETFUNC");
            return -1;
        }

        if (!strcmp(desc.name, pin_name))
            return desc.index;
    }

    DBG_PRINTF_ERROR("Programmable pin named %s not found.", pin_name);

    return -1;
}

//---------------------------------------------------------------------------------------

//---------------------------------------------------------------------------------------
// Get PHC device path for a network interface
// Returns phc_index, or -1 on error

int phc_get_index(const char *if_name) {
    struct ethtool_ts_info ts_info;
    struct ifreq ifr;
    int sock, ret;

    if (!if_name)
        return -1;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        DBG_PRINT_ERROR("Failed to create socket for ethtool query\n");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    memset(&ts_info, 0, sizeof(ts_info));
    strncpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);
    ts_info.cmd = ETHTOOL_GET_TS_INFO;
    ifr.ifr_data = (char *)&ts_info;

    ret = ioctl(sock, SIOCETHTOOL, &ifr);
    close(sock);

    if (ret < 0) {
        DBG_PRINTF_ERROR("Failed to get PHC index for interface %s (may not support hardware timestamping)\n", if_name);
        return -1;
    }

    if (ts_info.phc_index < 0) {
        DBG_PRINTF_WARNING("Interface %s does not have a PHC (phc_index=%d)\n", if_name, ts_info.phc_index);
        return -1;
    }

    return ts_info.phc_index;
}

//---------------------------------------------------------------------------------------
// Initialize PHC to system time for master mode
// Returns true if PHC was successfully set or is already synchronized

bool phc_init_to_system_time(const char *if_name, int32_t offset_ns) {
    if (!if_name)
        return false;

    int phc_index = phc_get_index(if_name);
    if (phc_index < 0)
        return false;

    char phc_device[32];
    snprintf(phc_device, sizeof(phc_device), "/dev/ptp%d", phc_index);

    clockid_t clkid = phc_open(phc_device);
    if (clkid == CLOCK_INVALID) {
        DBG_PRINTF_ERROR("Failed to open %s for PHC initialization\n", phc_device);
        return false;
    }

    // Check current PHC time
    struct timespec phc_ts, sys_ts;
    if (clock_gettime(clkid, &phc_ts) != 0 || clock_gettime(CLOCK_REALTIME, &sys_ts) != 0) {
        DBG_PRINT_ERROR("Failed to read clock times for PHC initialization\n");
        phc_close(clkid);
        return false;
    }

    long diff_sec = phc_ts.tv_sec - sys_ts.tv_sec;
    long abs_diff = labs(diff_sec);

    // DBG_PRINTF3("Initializing PHC %s to system time (current diff: %ld seconds)...\n", phc_device, diff_sec);

    // // Try to set PHC to system time
    sys_ts.tv_nsec += offset_ns;
    if (sys_ts.tv_nsec >= 1000000000L) {
        sys_ts.tv_sec += 1;
        sys_ts.tv_nsec -= 1000000000L;
    }
    if (sys_ts.tv_nsec < 0) {
        sys_ts.tv_sec -= 1;
        sys_ts.tv_nsec += 1000000000L;
    }
    if (clock_settime(clkid, &sys_ts) == 0) {
        DBG_PRINTF3("PHC %s successfully set to system time\n", phc_device);
        phc_close(clkid);
        return true;
    }

    // If direct set failed, try adjustment (may work on some systems)
    DBG_PRINTF_WARNING("clock_settime failed (%s), trying adjustment...\n", strerror(errno));

    struct timex tx;
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_SETOFFSET | ADJ_NANO;
    long long diff_ns = (long long)diff_sec * 1000000000LL + (sys_ts.tv_nsec - phc_ts.tv_nsec);
    tx.time.tv_sec = diff_ns / 1000000000LL;
    tx.time.tv_usec = diff_ns % 1000000000LL;

    if (clock_adjtime(clkid, &tx) == 0) {
        DBG_PRINTF3("PHC %s adjusted to system time\n", phc_device);
        phc_close(clkid);
        return true;
    }

    // Both methods failed
    if (errno == EBUSY) {
        DBG_PRINTF_WARNING("Cannot initialize PHC %s: Device busy (another process may be controlling it)\n", phc_device);
        DBG_PRINTF_WARNING("PHC will use its current time. Time diff: %ldh %ldm %lds\n", abs_diff / 3600, (abs_diff % 3600) / 60, abs_diff % 60);
    } else {
        DBG_PRINTF_ERROR("Failed to initialize PHC %s: %s\n", phc_device, strerror(errno));
    }

    phc_close(clkid);
    return false;
}

#endif // _LINUX
