/****************************************************************************
 * examples/elf/elf_main.c
 *
 *   Copyright (C) 2012 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <sys/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/ramdisk.h>
#include <nuttx/binfmt/binfmt.h>
#include <nuttx/binfmt/elf.h>

#include "tests/romfs.h"
#include "tests/dirlist.h"
#include "tests/symtab.h"

/****************************************************************************
 * Definitions
 ****************************************************************************/

/* Check configuration.  This is not all of the configuration settings that
 * are required -- only the more obvious.
 */

#if CONFIG_NFILE_DESCRIPTORS < 1
#  error "You must provide file descriptors via CONFIG_NFILE_DESCRIPTORS in your configuration file"
#endif

#ifndef CONFIG_ELF
#  error "You must select CONFIG_ELF in your configuration file"
#endif

#ifndef CONFIG_FS_ROMFS
#  error "You must select CONFIG_FS_ROMFS in your configuration file"
#endif

#ifdef CONFIG_DISABLE_MOUNTPOINT
#  error "You must not disable mountpoints via CONFIG_DISABLE_MOUNTPOINT in your configuration file"
#endif

#ifdef CONFIG_BINFMT_DISABLE
#  error "You must not disable loadable modules via CONFIG_BINFMT_DISABLE in your configuration file"
#endif

/* Describe the ROMFS file system */

#define SECTORSIZE   512
#define NSECTORS(b)  (((b)+SECTORSIZE-1)/SECTORSIZE)
#define MOUNTPT      "/mnt/romfs"

#ifndef CONFIG_EXAMPLES_ELF_DEVMINOR
#  define CONFIG_EXAMPLES_ELF_DEVMINOR 0
#endif

#ifndef CONFIG_EXAMPLES_ELF_DEVPATH
#  define CONFIG_EXAMPLES_ELF_DEVPATH "/dev/ram0"
#endif

/* If CONFIG_DEBUG is enabled, use dbg instead of printf so that the
 * output will be synchronous with the debug output.
 */

#ifdef CONFIG_CPP_HAVE_VARARGS
#  ifdef CONFIG_DEBUG
#    define message(format, arg...) dbg(format, ##arg)
#    define err(format, arg...)     dbg(format, ##arg)
#  else
#    define message(format, arg...) printf(format, ##arg)
#    define err(format, arg...)     fprintf(stderr, format, ##arg)
#  endif
#else
#  ifdef CONFIG_DEBUG
#    define message                 dbg
#    define err                     dbg
#  else
#    define message                 printf
#    define err                     printf
#  endif
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char delimiter[] =
  "****************************************************************************";

static char path[128];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: testheader
 ****************************************************************************/

static inline void testheader(FAR const char *progname)
{
  message("\n%s\n* Executing %s\n%s\n\n", delimiter, progname, delimiter);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: elf_main
 ****************************************************************************/

int elf_main(int argc, char *argv[])
{
  struct binary_s bin;
  int ret;
  int i;

  /* Initialize the ELF binary loader */

  message("Initializing the ELF binary loader\n");
  ret = elf_initialize();
  if (ret < 0)
    {
      err("ERROR: Initialization of the ELF loader failed: %d\n", ret);
      exit(1);
    }

  /* Create a ROM disk for the ROMFS filesystem */

  message("Registering romdisk at /dev/ram%d\n", CONFIG_EXAMPLES_ELF_DEVMINOR);
  ret = romdisk_register(CONFIG_EXAMPLES_ELF_DEVMINOR, (FAR uint8_t *)romfs_img,
                         NSECTORS(romfs_img_len), SECTORSIZE);
  if (ret < 0)
    {
      err("ERROR: romdisk_register failed: %d\n", ret);
      elf_uninitialize();
      exit(1);
    }

  /* Mount the file system */

  message("Mounting ROMFS filesystem at target=%s with source=%s\n",
         MOUNTPT, CONFIG_EXAMPLES_ELF_DEVPATH);

  ret = mount(CONFIG_EXAMPLES_ELF_DEVPATH, MOUNTPT, "romfs", MS_RDONLY, NULL);
  if (ret < 0)
    {
      err("ERROR: mount(%s,%s,romfs) failed: %s\n",
              CONFIG_EXAMPLES_ELF_DEVPATH, MOUNTPT, errno);
      elf_uninitialize();
    }

  /* Now excercise every progrm in the ROMFS file system */

  for (i = 0; dirlist[i]; i++)
    {
      testheader(dirlist[i]);

      memset(&bin, 0, sizeof(struct binary_s));
      snprintf(path, 128, "%s/%s", MOUNTPT, dirlist[i]);

      bin.filename = path;
      bin.exports  = exports;
      bin.nexports = NEXPORTS;

      ret = load_module(&bin);
      if (ret < 0)
        {
          err("ERROR: Failed to load program '%s'\n", dirlist[i]);
          exit(1);
        }

      ret = exec_module(&bin, 50);
      if (ret < 0)
        {
          err("ERROR: Failed to execute program '%s'\n", dirlist[i]);
          unload_module(&bin);
        }

      message("Wait a bit for test completion\n");
      sleep(4);
    }

  message("End-of-Test.. Exiting\n");
  return 0;
}
