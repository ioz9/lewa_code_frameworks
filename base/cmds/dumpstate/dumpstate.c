/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cutils/properties.h>

#include "private/android_filesystem_config.h"

#define LOG_TAG "dumpstate"
#include <utils/Log.h>

#include "dumpstate.h"

/* read before root is shed */
static char cmdline_buf[16384] = "(unknown)";
static const char *dump_traces_path = NULL;

/* dumps the current system state to stdout */
static void dumpstate() {
    time_t now = time(NULL);
    char build[PROPERTY_VALUE_MAX], fingerprint[PROPERTY_VALUE_MAX];
    char radio[PROPERTY_VALUE_MAX], bootloader[PROPERTY_VALUE_MAX];
    char network[PROPERTY_VALUE_MAX], date[80];

    property_get("ro.build.display.id", build, "(unknown)");
    property_get("ro.build.fingerprint", fingerprint, "(unknown)");
    property_get("ro.baseband", radio, "(unknown)");
    property_get("ro.bootloader", bootloader, "(unknown)");
    property_get("gsm.operator.alpha", network, "(unknown)");
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&now));

    printf("========================================================\n");
    printf("== dumpstate: %s\n", date);
    printf("========================================================\n");

    printf("\n");
    printf("Build: %s\n", build);
    printf("Bootloader: %s\n", bootloader);
    printf("Radio: %s\n", radio);
    printf("Network: %s\n", network);

    printf("Kernel: ");
    dump_file(NULL, "/proc/version");
    printf("Command line: %s\n", strtok(cmdline_buf, "\n"));
    printf("\n");

    dump_file("MEMORY INFO", "/proc/meminfo");
    run_command("CPU INFO", 10, "top", "-n", "1", "-d", "1", "-m", "30", "-t", NULL);
    run_command("PROCRANK", 20, "procrank", NULL);
    dump_file("VIRTUAL MEMORY STATS", "/proc/vmstat");
    dump_file("VMALLOC INFO", "/proc/vmallocinfo");
    dump_file("SLAB INFO", "/proc/slabinfo");
    dump_file("ZONEINFO", "/proc/zoneinfo");

    run_command("SYSTEM LOG", 20, "logcat", "-v", "time", "-d", "*:v", NULL);

    /* show the traces we collected in main(), if that was done */
    if (dump_traces_path != NULL) {
        dump_file("VM TRACES JUST NOW", dump_traces_path);
    }

    /* only show ANR traces if they're less than 15 minutes old */
    struct stat st;
    char anr_traces_path[PATH_MAX];
    property_get("dalvik.vm.stack-trace-file", anr_traces_path, "");
    if (!anr_traces_path[0]) {
        printf("*** NO VM TRACES FILE DEFINED (dalvik.vm.stack-trace-file)\n\n");
    } else if (stat(anr_traces_path, &st)) {
        printf("*** NO ANR VM TRACES FILE (%s): %s\n\n", anr_traces_path, strerror(errno));
    } else {
        dump_file("VM TRACES AT LAST ANR", anr_traces_path);
    }

    // dump_file("EVENT LOG TAGS", "/etc/event-log-tags");
    run_command("EVENT LOG", 20, "logcat", "-b", "events", "-v", "time", "-d", "*:v", NULL);
    run_command("RADIO LOG", 20, "logcat", "-b", "radio", "-v", "time", "-d", "*:v", NULL);

    run_command("NETWORK INTERFACES", 10, "netcfg", NULL);
    dump_file("NETWORK ROUTES", "/proc/net/route");
    dump_file("ARP CACHE", "/proc/net/arp");

#ifdef FWDUMP_bcm4329
    run_command("DUMP WIFI FIRMWARE LOG", 60,
            "su", "root", "dhdutil", "-i", "eth0", "upload", "/data/local/tmp/wlan_crash.dump", NULL);
#endif

    print_properties();

    run_command("KERNEL LOG", 20, "dmesg", NULL);

    dump_file("KERNEL WAKELOCKS", "/proc/wakelocks");
    dump_file("KERNEL CPUFREQ", "/sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state");

    run_command("VOLD DUMP", 10, "vdc", "dump", NULL);
    run_command("SECURE CONTAINERS", 10, "vdc", "asec", "list", NULL);

    run_command("PROCESSES", 10, "ps", "-P", NULL);
    run_command("PROCESSES AND THREADS", 10, "ps", "-t", "-p", "-P", NULL);
    run_command("LIBRANK", 10, "librank", NULL);

    dump_file("BINDER FAILED TRANSACTION LOG", "/sys/kernel/debug/binder/failed_transaction_log");
    dump_file("BINDER TRANSACTION LOG", "/sys/kernel/debug/binder/transaction_log");
    dump_file("BINDER TRANSACTIONS", "/sys/kernel/debug/binder/transactions");
    dump_file("BINDER STATS", "/sys/kernel/debug/binder/stats");
    dump_file("BINDER STATE", "/sys/kernel/debug/binder/state");

    run_command("FILESYSTEMS & FREE SPACE", 10, "df", NULL);

    dump_file("PACKAGE SETTINGS", "/data/system/packages.xml");
    dump_file("PACKAGE UID ERRORS", "/data/system/uiderrors.txt");

    dump_file("LAST KMSG", "/proc/last_kmsg");
    run_command("LAST RADIO LOG", 10, "parse_radio_log", "/proc/last_radio_log", NULL);
    dump_file("LAST PANIC CONSOLE", "/data/dontpanic/apanic_console");
    dump_file("LAST PANIC THREADS", "/data/dontpanic/apanic_threads");

    for_each_pid(show_wchan, "BLOCKED PROCESS WAIT-CHANNELS");

    printf("------ BACKLIGHTS ------\n");
    printf("LCD brightness=");
    dump_file(NULL, "/sys/class/leds/lcd-backlight/brightness");
    printf("Button brightness=");
    dump_file(NULL, "/sys/class/leds/button-backlight/brightness");
    printf("Keyboard brightness=");
    dump_file(NULL, "/sys/class/leds/keyboard-backlight/brightness");
    printf("ALS mode=");
    dump_file(NULL, "/sys/class/leds/lcd-backlight/als");
    printf("LCD driver registers:\n");
    dump_file(NULL, "/sys/class/leds/lcd-backlight/registers");
    printf("\n");

    printf("========================================================\n");
    printf("== Android Framework Services\n");
    printf("========================================================\n");

    /* the full dumpsys is starting to take a long time, so we need
       to increase its timeout.  we really need to do the timeouts in
       dumpsys itself... */
    run_command("DUMPSYS", 60, "dumpsys", NULL);
}

static void usage() {
    fprintf(stderr, "usage: dumpstate [-d] [-o file] [-s] [-z]\n"
            "  -d: append date to filename (requires -o)\n"
            "  -o: write to file (instead of stdout)\n"
            "  -s: write output to control socket (for init)\n"
            "  -z: gzip output (requires -o)\n");
}

int main(int argc, char *argv[]) {
    int do_add_date = 0;
    int do_compress = 0;
    char* use_outfile = 0;
    int use_socket = 0;

    LOGI("begin\n");

    /* set as high priority, and protect from OOM killer */
    setpriority(PRIO_PROCESS, 0, -20);
    FILE *oom_adj = fopen("/proc/self/oom_adj", "w");
    if (oom_adj) {
        fputs("-17", oom_adj);
        fclose(oom_adj);
    }

    /* very first thing, collect VM traces from Dalvik (needs root) */
    dump_traces_path = dump_vm_traces();

    int c;
    while ((c = getopt(argc, argv, "dho:svz")) != -1) {
        switch (c) {
            case 'd': do_add_date = 1;       break;
            case 'o': use_outfile = optarg;  break;
            case 's': use_socket = 1;        break;
            case 'v': break;  // compatibility no-op
            case 'z': do_compress = 6;       break;
            case '?': printf("\n");
            case 'h':
                usage();
                exit(1);
        }
    }

    /* open the vibrator before dropping root */
    FILE *vibrator = fopen("/sys/class/timed_output/vibrator/enable", "w");
    if (vibrator) fcntl(fileno(vibrator), F_SETFD, FD_CLOEXEC);

    /* read /proc/cmdline before dropping root */
    FILE *cmdline = fopen("/proc/cmdline", "r");
    if (cmdline != NULL) {
        fgets(cmdline_buf, sizeof(cmdline_buf), cmdline);
        fclose(cmdline);
    }

    if (getuid() == 0) {
        /* switch to non-root user and group */
        gid_t groups[] = { AID_LOG, AID_SDCARD_RW, AID_MOUNT };
        if (setgroups(sizeof(groups)/sizeof(groups[0]), groups) != 0) {
            LOGE("Unable to setgroups, aborting: %s\n", strerror(errno));
            return -1;
        }
        if (setgid(AID_SHELL) != 0) {
            LOGE("Unable to setgid, aborting: %s\n", strerror(errno));
            return -1;
        }
        if (setuid(AID_SHELL) != 0) {
            LOGE("Unable to setuid, aborting: %s\n", strerror(errno));
            return -1;
        }
    }

    char path[PATH_MAX], tmp_path[PATH_MAX];
    pid_t gzip_pid = -1;

    if (use_socket) {
        redirect_to_socket(stdout, "dumpstate");
    } else if (use_outfile) {
        strlcpy(path, use_outfile, sizeof(path));
        if (do_add_date) {
            char date[80];
            time_t now = time(NULL);
            strftime(date, sizeof(date), "-%Y-%m-%d-%H-%M-%S", localtime(&now));
            strlcat(path, date, sizeof(path));
        }
        strlcat(path, ".txt", sizeof(path));
        if (do_compress) strlcat(path, ".gz", sizeof(path));
        strlcpy(tmp_path, path, sizeof(tmp_path));
        strlcat(tmp_path, ".tmp", sizeof(tmp_path));
        gzip_pid = redirect_to_file(stdout, tmp_path, do_compress);
    }

    /* bzzzzzz */
    if (vibrator) {
        fputs("150", vibrator);
        fflush(vibrator);
    }

    dumpstate();

    /* bzzz bzzz bzzz */
    if (vibrator) {
        int i;
        for (i = 0; i < 3; i++) {
            fputs("75\n", vibrator);
            fflush(vibrator);
            usleep((75 + 50) * 1000);
        }
        fclose(vibrator);
    }

    /* wait for gzip to finish, otherwise it might get killed when we exit */
    if (gzip_pid > 0) {
        fclose(stdout);
        waitpid(gzip_pid, NULL, 0);
    }

    /* rename the (now complete) .tmp file to its final location */
    if (use_outfile && rename(tmp_path, path)) {
        fprintf(stderr, "rename(%s, %s): %s\n", tmp_path, path, strerror(errno));
    }

    LOGI("done\n");

    return 0;
}
