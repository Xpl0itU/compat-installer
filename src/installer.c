/* Wii title installer for Wii U Mode
 *   Copyright (C) 2021  TheLordScruffy
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

#include "installer.h"
#include <gctypes.h>
#include <iosuhax.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define CINS_DEBUG

#ifdef CINS_DEBUG
#   define CINS_Log(...) do {                           \
        char _wupi_print_str[256];                      \
        snprintf(_wupi_print_str, 255, __VA_ARGS__);    \
    } while (0)
#endif

#define CINS_STAGE_INIT           1
#define CINS_STAGE_TICKET         2
#define CINS_STAGE_TITLEDIR       3
#define CINS_STAGE_TMD            4
#define CINS_STAGE_CONTENT        5
#define CINS_STAGE_FINAL          6
#define CINS_STAGE_FINAL_DATA     7
#define CINS_STAGE_DELETE_TIK     8
#define CINS_STAGE_DELETE_TITLE   9

#define IOS_SUCCESS         0
#define FS_STATUS_EXISTS    -0x30016
#define FS_STATUS_NOT_FOUND -0x30017

#define CINS_PATH_LEN       (sizeof("slc:") + 63)

#define CINS_ID_HI ((u32)(CINS_TITLEID >> 32))
#define CINS_ID_LO ((u32)(CINS_TITLEID & 0xFFFFFFFF))

#define CINS_TRY(c) if (!(c))                                                  \
    do {                                                                       \
        CINS_Log("'%s' failed with errno: %d\n", #c, errno);                   \
        ret = errno < 0 ? errno : -errno;                                      \
        goto error;                                                            \
    } while(0)


s32
CINS_Install (
    const void* ticket, u32 ticket_size,
    const void* tmd, u32 tmd_size, 
    CINS_Content* contents, u16 numContents, int fsaFd
)
{
    s32 ret, stage, i;
    int fd = -1;
    char path[CINS_PATH_LEN], pathd[CINS_PATH_LEN];
    char titlePath[CINS_PATH_LEN], ticketPath[CINS_PATH_LEN], ticketFolder[CINS_PATH_LEN];
    
    CINS_Log("Starting install\n");

    /* This installer originally created a temporary directory for the
     * installation, wrote everything to flash there, then renamed it all to
     * other directories. The wupserver doesn't already support renaming files,
     * and my attempt to add it failed so I gave up. */
    snprintf(titlePath, CINS_PATH_LEN,
             "slc:/title/%08x/%08x", CINS_ID_HI, CINS_ID_LO);
    snprintf(path, CINS_PATH_LEN,
             "slc:/title/%08x", CINS_ID_HI);
    snprintf(ticketPath, CINS_PATH_LEN,
             "slc:/ticket/%08x/%08x.tik", CINS_ID_HI, CINS_ID_LO);
    snprintf(ticketFolder, CINS_PATH_LEN,
             "slc:/ticket/%08x", CINS_ID_HI);
    /* Init stage is not needed anymore. */

    CINS_Log("Writing ticket...\n");
    stage = CINS_STAGE_TICKET;
    {
        unlink(ticketPath);

        ret = mkdir(ticketFolder, -1);
        if (ret == 0 || errno == FS_STATUS_EXISTS)
        {
            CINS_TRY(fd = fopen(ticketPath, "wb"));
            CINS_TRY(fwrite(ticket, ticket_size, 1, fd) == 1);

            fclose(fd);
            fd = NULL;

            ret = 0;
        }

        CINS_TRY (!ret); // ret == 0
    }

    CINS_Log("Creating title directory...\n");
    stage = CINS_STAGE_TITLEDIR;
    {
        /* Create the title directory if it doesn't already exist. The first
         * word (type) should exist, but the second one (the unique title)
         * shouldn't unless there is save data. */
        //ret = mkdir(path, -1);
        ret = IOSUHAX_FSA_MakeDir(fsaFd, "slc:/title/00010001/4f484243", -1);
        if (ret == 0 || errno == FS_STATUS_EXISTS)
        {
            ret = IOSUHAX_FSA_MakeDir(fsaFd, titlePath, -1);
            if (ret != 0 && errno == FS_STATUS_EXISTS) {
                /* The title is already installed, delete content but preserve
                 * the data directory. */
                CINS_Log(
                    "Title directory already exists, deleting content...\n"
                );
                if (IOSUHAX_FSA_Remove(fsaFd, "slc:/title/00010001/4f484243/content") == 0 || errno == FS_STATUS_NOT_FOUND)
                    ret = 0;
            }
        }

        CINS_TRY (!ret); // ret == 0

        /* This directory is necessary for the Wii Menu to function
         * correctly, but also don't overwrite any data that might already
         * exist. */
        if (IOSUHAX_FSA_MakeDir(fsaFd, "slc:/title/00010001/4f484243/data", -1) != 0 && errno != FS_STATUS_EXISTS) {
            CINS_Log("Failed to create the data directory, ret = %d\n", ret);
            goto error;
        }
        CINS_TRY (IOSUHAX_FSA_MakeDir(fsaFd, "slc:/title/00010001/4f484243/content", -1) == 0);
    }

    CINS_Log("Writing TMD...\n");
    stage = CINS_STAGE_TMD;
    {
        //CINS_TRY (fd = fopen(path, "wb"));
        CINS_TRY(fd = IOSUHAX_FSA_OpenFile(fsaFd, "slc:/title/00010001/4f484243/content/title.tmd", "wb", &fd));
        //CINS_TRY (fwrite(tmd, tmd_size, 1, fd) == 1);
        CINS_TRY(IOSUHAX_FSA_WriteFile(fsaFd, tmd, tmd_size, 1, fd, 0) == 1);

        //fclose(fd);
        IOSUHAX_FSA_CloseFile(fsaFd, fd);
        fd = 0;
    }

    CINS_Log("Writing contents...\n");
    stage = CINS_STAGE_CONTENT;
    {
        for (i = 0; i < numContents; i++)
        {
            //CINS_Log("Writing content %08x.app\n", i);
            snprintf(path, CINS_PATH_LEN,
                     "slc:/title/00010001/4f484243/content/%08x.app", i);

            //CINS_TRY (fd = fopen(path, "wb"));
            CINS_TRY(fd = IOSUHAX_FSA_OpenFile(fsaFd, path, "wb", &fd));
            //CINS_TRY (fwrite(contents[i].data, contents[i].length, 1, fd) == 1);
            CINS_TRY(IOSUHAX_FSA_WriteFile(fsaFd, contents[i].data, contents[i].length, 1, fd, 0) == 1);

            //fclose(fd);
            IOSUHAX_FSA_CloseFile(fsaFd, fd);
            fd = 0;
        }
    }
    ret = IOS_SUCCESS;
    CINS_Log("Install succeeded!\n");

error:
    if (fd != 0)
        //fclose(fd);
        IOSUHAX_FSA_CloseFile(fsaFd, fd);
    if (ret < 0)
    {
        CINS_Log("Install failed, attempting to delete title...\n");
        /* Installation failed in the final stages. Delete these to be sure
         * there is no 'half installed' title lurking in the filesystem. */
        //unlink(titlePath);
        //unlink(ticketPath);
    }

    if (ret < -0x99999)
        ret = -0x800;
    if (ret > 0)
        ret = 0;
    return ret < 0 ? ret - stage * 0x100000 : 0;
}