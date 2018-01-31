/*
**  %Z% %I% %W% %G% %U%
**
**  ZZ_Copyright_BEGIN
**
**
**  Licensed Materials - Property of IBM
**
**  IBM Linear Tape File System Single Drive Edition Version 2.2.1 for Linux and Mac OS X
**
**  Copyright IBM Corp. 2010, 2016
**
**  This file is part of the IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X
**  (formally known as IBM Linear Tape File System)
**
**  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is free software;
**  you can redistribute it and/or modify it under the terms of the GNU Lesser
**  General Public License as published by the Free Software Foundation,
**  version 2.1 of the License.
**
**  The IBM Linear Tape File System Single Drive Edition for Linux and Mac OS X is distributed in the
**  hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
**  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
**  See the GNU Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
**  or download the license from <http://www.gnu.org/licenses/>.
**
**
**  ZZ_Copyright_END
**
*************************************************************************************
**
** COMPONENT NAME:  IBM Linear Tape File System
**
** FILE NAME:       tape_drivers/generic/file/filedebug_tc.c
**
** DESCRIPTION:     Implements a file-based tape simulator.
**
** AUTHORS:         Atsushi Abe
**                  IBM Yamato, Japan
**                  PISTE@jp.ibm.com
**
**                  Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
*************************************************************************************
*/
#ifdef mingw_PLATFORM
#include "libltfs/arch/win/win_util.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <libgen.h>
#include <dirent.h>

#include "libltfs/ltfs_fuse_version.h"
#include <fuse.h>

#include "ltfs_copyright.h"
#include "libltfs/ltfslogging.h"
#include "libltfs/ltfs_endian.h"
#include "libltfs/tape_ops.h"
#include "libltfs/ltfs_error.h"

#include "filedebug_conf_tc.h"

volatile char *copyright = LTFS_COPYRIGHT_0"\n"LTFS_COPYRIGHT_1"\n"LTFS_COPYRIGHT_2"\n" \
	LTFS_COPYRIGHT_3"\n"LTFS_COPYRIGHT_4"\n"LTFS_COPYRIGHT_5"\n";

/* Default directory where the emulated tape contents go to */
#ifdef mingw_PLATFORM
const char *filedebug_default_device = "c:\\tmp\\ltfs\\tape";
#else
const char *filedebug_default_device = "/tmp/ltfs/tape";
#endif

#define MAX_PARTITIONS 2
#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)
#define FILE_DEBUG_MAX_BLOCK_SIZE (4 * MB)

/* O_BINARY is defined only in MinGW */
#ifndef O_BINARY
#define O_BINARY 0
#endif

#define MISSING_EOD      (0xFFFFFFFFFFFFFFFFLL)
#define CARTRIDGE_CONFIG  "filedebug_tc_conf.xml"

#define THREASHOLD_FORCE_WRITE_NO_WRITE (5)
#define DEFAULT_WRITEPERM               (0)
#define DEFAULT_READPERM                (0)

/* For drive link feature */
#ifdef mingw_PLATFORM
#define DRIVE_LIST_DIR    "ltfs"
#else
#define DRIVE_LIST_DIR    "/tmp"
#endif

/**
 * Emulator-specific data structures, used in lieu of a file descriptor
 */
struct filedebug_data {
	int fd;                              /**< File descriptor to contain the pointer to directory where blocks reside */
	char *dirbase;                       /**< Base directory for searcing directoty from the pointer */
	char *dirname;                       /**< Directory where blocks reside */
	bool device_reserved;                /**< True when the device has been successfully reserved */
	bool medium_locked;                  /**< True when preventing medium removal by the user */
	bool null_backend;                   /**< True when runiing on null backend_mode */
	struct tc_position current_position; /**< Current tape position (partition, block) */
	uint32_t max_block_size;             /**< Maximum block size, in bytes */
	bool ready;                          /**< Is the "tape" loaded? */
	uint64_t last_block[MAX_PARTITIONS]; /**< Last positions for all partitions */
	uint64_t eod[MAX_PARTITIONS];        /**< Append positions (1 + last block) for all partitions */
	int  partitions;                     /**< Number of available partitions */
	uint64_t write_pass_prev;            /**< Previous write Pass */
	uint64_t write_pass;                 /**< Current write Pass of LTO drive for consistency check*/
	unsigned p0_warning;                 /**< Nonzero to provide early warning on partition 0 */
	unsigned p1_warning;                 /**< Nonzero to provide early warning on partition 1 */
	unsigned p0_p_warning;               /**< Nonzero to provide programmable early warning on partition 0 */
	unsigned p1_p_warning;               /**< Nonzero to provide programmable early warning on partition 1 */
	uint64_t force_writeperm;            /**< pseudo write perm threshold */
	uint64_t force_readperm;             /**< pseudo read perm threashold */
	uint64_t write_counter;              /**< write call counter for pseudo write perm */
	uint64_t read_counter;               /**< read call counter for pseudo write perm */
	char *serial_number;                 /**< Serial number of this dummy tape device */
	char *product_id;                    /**< Product ID of this dummy tape device */
	struct filedebug_conf_tc conf;       /**< Bahavior option for this instance */
};

/* record suffixes for data block, filemark, EOD indicator */
long original_pid = 0;
static const char *rec_suffixes = "RFE";
#define SUFFIX_RECORD   (0)
#define SUFFIX_FILEMARK (1)
#define SUFFIX_EOD      (2)

/* local prototypes */
int filedebug_search_eod(struct filedebug_data *state, int partition);
int _filedebug_write_eod(struct filedebug_data *state);
int _filedebug_check_file(const char *fname);
char *_filedebug_make_current_filename(const struct filedebug_data *state, char type);
char *_filedebug_make_filename(const struct filedebug_data *state,
	int part, uint64_t pos, char type);
char *_filedebug_make_attrname(const struct filedebug_data *state, int part, int id);
int _filedebug_remove_current_record(const struct filedebug_data *state);
int _filedebug_remove_record(const struct filedebug_data *state,
	int partition, uint64_t blknum);
int _filedebug_space_fm(struct filedebug_data *state, uint64_t count, bool back);
int _filedebug_space_rec(struct filedebug_data *state, uint64_t count, bool back);
int _get_wp(struct filedebug_data *state, uint64_t *wp);
int _set_wp(struct filedebug_data *state, uint64_t wp);

/* Command-line options recognized by this module */
#define FILEDEBUG_OPT(templ,offset,value) { templ, offsetof(struct filedebug_data, offset), value }

static inline uint64_t calc_p0_cap(struct filedebug_data *state)
{
	/* 5% of total capacity */
	return (state->conf.capacity_mb * 5 /100);
}

static inline uint64_t calc_p0_remaining(struct filedebug_data *state)
{
	/* Assume 512KB per 1 record */
	return (calc_p0_cap(state) - state->eod[0] / 2);
}

static inline uint64_t calc_p1_cap(struct filedebug_data *state)
{
	return (state->conf.capacity_mb - calc_p0_cap(state));
}

static inline uint64_t calc_p1_remaining(struct filedebug_data *state)
{
	/* Assume 512KB per 1 record */
	return (calc_p1_cap(state) - state->eod[1] / 2);
}

static struct fuse_opt filedebug_opts[] = {
	FUSE_OPT_END
};

int null_parser(void *priv, const char *arg, int key, struct fuse_args *outargs)
{
	return 1;
}

int filedebug_parse_opts(void *vstate, void *opt_args)
{
	struct filedebug_data *state = (struct filedebug_data *) vstate;
	struct fuse_args *args = (struct fuse_args *) opt_args;
	int ret;

	/* fuse_opt_parse can handle a NULL device parameter just fine */
	ret = fuse_opt_parse(args, state, filedebug_opts, null_parser);
	if (ret < 0)
		return ret;

	return 0;
}

void filedebug_help_message(void)
{
	ltfsresult("12272I", filedebug_default_device);
}

int filedebug_open(const char *name, void **handle)
{
	struct filedebug_data *state;
	struct stat d;
	char *tmp = NULL;
	char *cur, *p;
	char *pid = NULL, *ser = NULL;
	int ret, i;

	ltfsmsg(LTFS_INFO, "12260I", name);

	CHECK_ARG_NULL(handle, -LTFS_NULL_ARG);
	*handle = NULL;

	state = (struct filedebug_data *)calloc(1,sizeof(struct filedebug_data));
	if (!state) {
		ltfsmsg(LTFS_ERR, "10001E", "filedebug_open: private data");
		return -EDEV_NO_MEMORY;
	}

	/* check name is file or dir */
	ret = stat(name, &d);
	if (ret == 0 && S_ISREG(d.st_mode)) {
		/* Run on file mode */
		ltfsmsg(LTFS_INFO, "12259I", name);
		state->fd = open(name, O_RDWR | O_BINARY);
		if (state->fd < 0) {
			ltfsmsg(LTFS_ERR, "12263E", name);
			return -EDEV_INTERNAL_ERROR;
		}

		/* Parse pid and serial from filename */
		cur = (char*)name;
		cur += strlen(name) - 1;
		for (i = 0; i < (int)strlen(name); i++) {
			if (*cur == '.')
				pid = cur + 1;
			if (*cur == '_') {
				ser = cur + 1;
				break;
			}
			cur --;
		}

		if (pid && ser) {
			ret = asprintf(&state->serial_number, "%s", ser);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, "10001E", "filedebug_open: serial & pid");
				free(state);
				return -EDEV_NO_MEMORY;
			}

			for (i = 0; i < (int)strlen(state->serial_number); i++) {
				if (state->serial_number[i] == '.') {
					state->serial_number[i] = '\0';
					state->product_id = &(state->serial_number[i+1]);
					break;
				}
			}
		}

		/* Store directory base */
		tmp = strdup(name);
		if (!tmp) {
			ltfsmsg(LTFS_ERR, "10001E", "filedebug_open: dirbase");
			free(state);
			return -EDEV_NO_MEMORY;
		}

		/* The dirname() function may return a pointer to static storage
		   that may then be overwritten by subsequent calls to dirname(). */
		p = dirname(tmp);
		state->dirbase = (char *) calloc(strlen(p) + 1, sizeof(char));
		if (!state->dirbase) {
			ltfsmsg(LTFS_ERR, "10001E", "filedebug_open: dirbase");
			free(state);
			free(tmp);
			return -EDEV_NO_MEMORY;
		}
		strcpy(state->dirbase, p);
		free(tmp);
	} else {
		/* make sure directory exists */
		ltfsmsg(LTFS_INFO, "12258I", name);
		if (ret || !S_ISDIR(d.st_mode)) {
			ltfsmsg(LTFS_ERR, "12270E", name);
			free(state);
			return -LTFS_INVALID_SRC_PATH;
		}

		state->dirname = strdup(name);
		if (!state->dirname) {
			ltfsmsg(LTFS_ERR, "10001E", "filedebug_open: dirname");
			free(state);
			return -EDEV_NO_MEMORY;
		}
	}

	state->ready          = false;
	state->max_block_size = 16 * MB;

	/* Set default option value */
	state->conf.dummy_io         = false;
	state->conf.emulate_readonly = false;
	state->conf.capacity_mb      = 3 * (GB / MB);
	state->conf.cart_type        = TC_MP_LTO5D_CART;
	state->conf.density_code     = 0x58;

	state->force_writeperm = DEFAULT_WRITEPERM;
	state->force_readperm  = DEFAULT_READPERM;

	*handle = (void *) state;
	return 0;
}

int filedebug_reopen(const char *name, void *vstate)
{
	/* Do nothing */
	return 0;
}

int filedebug_close(void *vstate)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	/* Write EOD of DP here when dummy io mode is enebaled */
	if (state->conf.dummy_io) {
		state->current_position.partition = 1;
		state->current_position.block = state->eod[1];
		_filedebug_write_eod(state);
	}

	if (state) {
		if (state->fd > 0)
			close(state->fd);
		if (state->dirbase)
			free(state->dirbase);
		if (state->dirname)
			free(state->dirname);
		if (state->serial_number)
			free(state->serial_number);
		free(state);
	}

	return 0;
}

int filedebug_close_raw(void *vstate)
{
	return 0;
}

int filedebug_is_connected(const char *devname)
{
	return 0;
}

int filedebug_inquiry(void *vstate, struct tc_inq *inq)
{
	memset(inq, 0, sizeof(struct tc_inq));
	memcpy(inq->vid, "DUMMY   ", 8);
	memcpy(inq->pid, "DUMMYDEV        ", 16);
	memcpy(inq->revision, "0000", 4);
	/* Do not fill inq->vendor for vendor specific data */
	return DEVICE_GOOD;
}

int filedebug_inquiry_page(void *vstate, unsigned char page, struct tc_inq_page *inq)
{
	memset(inq, 0, sizeof(struct tc_inq_page));
	return DEVICE_GOOD;
}

int filedebug_test_unit_ready(void *vstate)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	if (!state->ready)
		return -EDEV_NEED_INITIALIZE;
	return DEVICE_GOOD;
}

int filedebug_read(void *vstate, char *buf, size_t count, struct tc_position *pos,
	const bool unusual_size)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	char *fname;
	size_t fname_len;
	int ret;
	ssize_t bytes_read;
	int fd;

	ltfsmsg(LTFS_DEBUG, "12161D", count, state->current_position.partition,
		(unsigned long long)state->current_position.block,
		(unsigned long long)state->current_position.filemarks);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12162E");
		return -EDEV_NOT_READY;
	}

	/* check for EOD (reading is an error) */
	if (state->eod[state->current_position.partition] == state->current_position.block) {
		return -EDEV_EOD_DETECTED;
	}

	if (state->conf.dummy_io &&
		state->current_position.partition &&
		state->current_position.block > 6) {

		/*
		 *  Dummy I/O mode
		 *  Do not write any data on partition1 (DP)
		 */

		++state->current_position.block;
		pos->block = state->current_position.block;
		return count;

	} else {
		fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_EOD]);
		if (!fname)
			return -EDEV_NO_MEMORY;
		fname_len = strlen(fname);

		ret = _filedebug_check_file(fname);
		if (ret < 0) {
			free(fname);
			return ret;
		}
		if (ret > 0) {
			ltfsmsg(LTFS_ERR, "12163E");
			free(fname);
			return -EDEV_EOD_NOT_FOUND;
		}

		/* check for filemark (reading returns 0 bytes and advances the position) */
		fname[fname_len - 1] = rec_suffixes[SUFFIX_FILEMARK];
		ret = _filedebug_check_file(fname);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "12164E", ret);
			free(fname);
			return ret;
		}
		if (ret > 0) {
			free(fname);
			++state->current_position.block;
			++state->current_position.filemarks;
			pos->block = state->current_position.block;
			pos->filemarks = state->current_position.filemarks;
			return 0;
		}

		/* check for record */
		fname[fname_len - 1] = rec_suffixes[SUFFIX_RECORD];
		ret = _filedebug_check_file(fname);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "12165E", ret);
			free(fname);
			return ret;
		}
		if (ret > 0) {
			fd = open(fname, O_RDONLY | O_BINARY);
			free(fname);
			if (fd < 0) {
				ltfsmsg(LTFS_ERR, "12166E", errno);
				return -EDEV_RW_PERM;
			}

			/* TODO: return -EDEV_INVALID_ARG if buffer is too small to hold complete record? */
			bytes_read = read(fd, buf, count);
			if (bytes_read < 0) {
				ltfsmsg(LTFS_ERR, "12167E", errno);
				close(fd);
				return -EDEV_RW_PERM;
			}

			ret = close(fd);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, "12168E", errno);
				return -EDEV_RW_PERM;
			}

			++state->current_position.block;
			pos->block = state->current_position.block;

			ltfsmsg(LTFS_DEBUG, "12169D", bytes_read);
			return bytes_read;
		}

		/* couldn't find any records?! something is corrupted */
		ltfsmsg(LTFS_ERR, "12170E");
		free(fname);
	}

	return -EDEV_RW_PERM;
}

int filedebug_write(void *vstate, const char *buf, size_t count, struct tc_position *pos)
{
	int rc = -1;
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	char *fname;
	int fd;
	ssize_t written;

	ltfsmsg(LTFS_DEBUG, "12171D", count, state->current_position.partition,
		(unsigned long long)state->current_position.block,
		(unsigned long long)state->current_position.filemarks);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12172E");
		rc = -EDEV_NOT_READY;
		return rc;
	}

	if (! buf && count > 0) {
		ltfsmsg(LTFS_ERR, "12173E");
		rc = -EDEV_INVALID_ARG;
		return rc;
	} else if (count == 0) {
		rc = 0; /* nothing to do */
		return rc;
	}

	if ( state->force_writeperm ) {
		state->write_counter++;
		if ( state->write_counter > state->force_writeperm ) {
			ltfsmsg(LTFS_INFO, "12227I", "write");
			return -EDEV_WRITE_PERM;
		} else if ( state->write_counter > (state->force_writeperm - THREASHOLD_FORCE_WRITE_NO_WRITE) ) {
			ltfsmsg(LTFS_INFO, "12228I");
			pos->block++;
			return DEVICE_GOOD;
		}
	}

	if (count > (size_t)state->max_block_size) {
		ltfsmsg(LTFS_ERR, "12174E", count, (size_t)state->max_block_size);
		rc = -EDEV_INVALID_ARG;
		return rc;
	}

	if (state->conf.dummy_io &&
		state->current_position.partition &&
		state->current_position.block > 6) {

		/*
		 *  Dummy I/O mode
		 *  Do not write any data on partition1 (DP)
		 */

		++state->current_position.block;
		pos->block = state->current_position.block;
		state->eod[state->current_position.partition] = state->current_position.block;
		written = count;

	} else {
		/* clean up old records at this position */
		rc = _filedebug_remove_current_record(state);
		if (rc < 0) {
			ltfsmsg(LTFS_ERR, "12175E", rc);
			return rc;
		}

		/* Increment Write Pass for consistency check */
		if(state->write_pass_prev == state->write_pass){
			rc = _set_wp(vstate, ++(state->write_pass));
			if (rc < 0) {
				ltfsmsg(LTFS_ERR, "12176E", rc);
				return rc;
			}
		}

		/* create the file */
		fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_RECORD]);
		if (!fname) {
			ltfsmsg(LTFS_ERR, "12177E");
			rc = -EDEV_NO_MEMORY;
			return rc;
		}
		fd = open(fname,
				  O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
				  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
		if (fd < 0) {
			ltfsmsg(LTFS_ERR, "12178E", fname, errno);
			free(fname);
			return -EDEV_RW_PERM;
		}
		free(fname);

		/* write and close the file */
		written = write(fd, buf, count);
		if (written < 0) {
			ltfsmsg(LTFS_ERR, "12179E", errno);
			close(fd);
			return -EDEV_RW_PERM;
		}
		rc = close(fd);
		if (rc < 0) {
			ltfsmsg(LTFS_ERR, "12180E", errno);
			return -EDEV_RW_PERM;
		}

		/* clean up old records */
		++state->current_position.block;
		pos->block = state->current_position.block;

		rc = _filedebug_write_eod(state);
		if (rc < 0) {
			ltfsmsg(LTFS_ERR, "12181E", rc);
			return rc;
		}
	}

	rc = written;
	if (state->p0_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_warning)
		pos->early_warning = true;
	else if (state->p1_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_warning)
		pos->early_warning = true;
	/* Programmable early warning is set only when position moves into
	   programmable early warning zone in write() method. */
	if (state->p0_p_warning && state->current_position.partition == 0 &&
		state->current_position.block == state->p0_p_warning)
		pos->programmable_early_warning = true;
	else if (state->p1_p_warning && state->current_position.partition == 1 &&
		state->current_position.block == state->p1_p_warning)
		pos->programmable_early_warning = true;
	return rc;
}

int filedebug_writefm(void *vstate, size_t count, struct tc_position *pos, bool immed)
{
	int rc = -1;
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	char *fname;
	int fd;
	size_t i;

	ltfsmsg(LTFS_DEBUG, "12182D", count, state->current_position.partition,
		(unsigned long long)state->current_position.block,
		(unsigned long long)state->current_position.filemarks);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12183E");
		rc = -EDEV_NOT_READY;
		return rc;
	}

	/* Do nothing in case of WFM 0 */
	if (count == 0) {
		return DEVICE_GOOD;
	}

	if (state->conf.dummy_io &&
		state->current_position.partition &&
		state->current_position.block > 6) {

		/*
		 *  Dummy I/O mode
		 *  Do not write any data on partition1 (DP)
		 */

		for (i=0; i<count; ++i) {
			++state->current_position.block;
			++state->current_position.filemarks;
			pos->block = state->current_position.block;
			pos->filemarks = state->current_position.filemarks;
		}
		state->eod[state->current_position.partition] = state->current_position.block;
		rc = DEVICE_GOOD;

	} else {
		/* Increment Write Pass for consistency check */
		if(state->write_pass_prev == state->write_pass){
			rc = _set_wp(vstate, ++(state->write_pass));
			if (rc < 0) {
				ltfsmsg(LTFS_ERR, "12184E", rc);
				return rc;
			}
		}

		for (i=0; i<count; ++i) {
			rc = _filedebug_remove_current_record(state);
			if (rc < 0) {
				ltfsmsg(LTFS_ERR, "12185E", rc);
				return rc;
			}

			fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_FILEMARK]);
			if (!fname) {
				ltfsmsg(LTFS_ERR, "12186E");
				rc = -EDEV_NO_MEMORY;
				return rc;
			}

			fd = open(fname,
					  O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
					  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
			if (fd < 0) {
				ltfsmsg(LTFS_ERR, "12187E", fname, errno);
				free(fname);
				return -EDEV_RW_PERM;
			}
			free(fname);

			rc = close(fd);
			if (rc < 0) {
			ltfsmsg(LTFS_ERR, "12188E", errno);
			return -EDEV_RW_PERM;
			}

			++state->current_position.block;
			++state->current_position.filemarks;
			pos->block = state->current_position.block;
			pos->filemarks = state->current_position.filemarks;

			rc = _filedebug_write_eod(state);
			if (rc < 0) {
				ltfsmsg(LTFS_ERR, "12189E", rc);
				return rc;
			}
		}
	}

	if (state->p0_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_warning)
		pos->early_warning = true;
	else if (state->p1_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_warning)
		pos->early_warning = true;
	if (state->p0_p_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_p_warning)
		pos->programmable_early_warning = true;
	else if (state->p1_p_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_p_warning)
		pos->programmable_early_warning = true;
	return rc;
}

int filedebug_rewind(void *vstate, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12190E");
		return -EDEV_NOT_READY;
	}

	/* Does rewinding reset the partition? */
	state->current_position.block     = 0;
	state->current_position.filemarks = 0;
	state->write_counter              = 0;
	state->read_counter               = 0;
	pos->block = state->current_position.block;
	pos->filemarks = 0;
	pos->early_warning = false;
	pos->programmable_early_warning = false;

	return DEVICE_GOOD;
}

int filedebug_locate(void *vstate, struct tc_position dest, struct tc_position *pos)
{
	int rc = 0;
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	tape_filemarks_t count_fm = 0;
	tape_block_t     i;

	ltfsmsg(LTFS_DEBUG, "12152D", "locate", (unsigned long long)dest.partition,
		(unsigned long long)dest.block);

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12191E");
		rc = -EDEV_NOT_READY;
		return rc;
	}
	if (dest.partition >= MAX_PARTITIONS) {
		ltfsmsg(LTFS_ERR, "12192E", (unsigned long)dest.partition);
		rc = -EDEV_INVALID_ARG;
		return rc;
	}

	state->current_position.partition = dest.partition;
	if (state->eod[dest.partition] == MISSING_EOD &&
		state->last_block[dest.partition] < dest.block)
			state->current_position.block = state->last_block[dest.partition] + 1;
	else if (state->eod[dest.partition] < dest.block)
		state->current_position.block = state->eod[dest.partition];
	else
		state->current_position.block = dest.block;
	pos->partition = state->current_position.partition;
	pos->block     = state->current_position.block;

	for(i = 0; i < state->current_position.block; ++i) {
		char *fname;

		fname = _filedebug_make_filename(state, state->current_position.partition,
										 i, rec_suffixes[SUFFIX_FILEMARK]);
		if (!fname) {
			ltfsmsg(LTFS_ERR, "12193E");
			rc = -EDEV_NO_MEMORY;
			return rc;
		}

		rc = _filedebug_check_file(fname);
		if (rc == 1)
			++count_fm;
		free(fname);
	}

	rc = 0;
	state->current_position.filemarks = count_fm;
	pos->filemarks = state->current_position.filemarks;

	if (state->p0_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_warning)
		pos->early_warning = true;
	else if (state->p1_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_warning)
		pos->early_warning = true;
	if (state->p0_p_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_p_warning)
		pos->programmable_early_warning = true;
	else if (state->p1_p_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_p_warning)
		pos->programmable_early_warning = true;
	return rc;
}

int filedebug_space(void *vstate, size_t count, TC_SPACE_TYPE type, struct tc_position *pos)
{
	int rc = 0;
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	int ret_fm;
	tape_filemarks_t count_fm = 0;
	tape_block_t     i;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12194E");
		rc = -EDEV_NOT_READY;
		return rc;
	}

	switch(type) {
		case TC_SPACE_EOD:
			ltfsmsg(LTFS_DEBUG, "12153D", "space to EOD");
			state->current_position.block = state->eod[state->current_position.partition];
			if(state->current_position.block == MISSING_EOD) {
				rc = -EDEV_RW_PERM;
				return rc;
			} else
				rc = 0;
			break;
		case TC_SPACE_FM_F:
			ltfsmsg(LTFS_DEBUG, "12154D", "space forward file marks", (unsigned long long)count);
			if(state->current_position.block == MISSING_EOD) {
				rc = -EDEV_RW_PERM;
				return rc;
			} else
				rc = _filedebug_space_fm(state, count, false);
			break;
		case TC_SPACE_FM_B:
			ltfsmsg(LTFS_DEBUG, "12154D", "space back file marks", (unsigned long long)count);
			if(state->current_position.block == MISSING_EOD) {
				rc = -EDEV_RW_PERM;
				return rc;
			} else
				rc = _filedebug_space_fm(state, count, true);
			break;
		case TC_SPACE_F:
			ltfsmsg(LTFS_DEBUG, "12154D", "space forward records", (unsigned long long)count);
			if(state->current_position.block == MISSING_EOD) {
				rc = -EDEV_RW_PERM;
				return rc;
			} else
				rc = _filedebug_space_rec(state, count, false);
			break;
		case TC_SPACE_B:
			ltfsmsg(LTFS_DEBUG, "12154D", "space back records", (unsigned long long)count);
			if(state->current_position.block == MISSING_EOD) {
				rc = -EDEV_RW_PERM;
				return rc;
			} else
				rc = _filedebug_space_rec(state, count, true);
			break;
		default:
			ltfsmsg(LTFS_ERR, "12127E");
			rc = -EDEV_INVALID_ARG;
			return rc;
	}

	pos->block = state->current_position.block;

	for(i = 0; i < state->current_position.block; ++i) {
		char *fname;

		fname = _filedebug_make_filename(state, state->current_position.partition,
										 i, rec_suffixes[SUFFIX_FILEMARK]);
		if (!fname) {
			ltfsmsg(LTFS_ERR, "12195E");
			rc = -EDEV_NO_MEMORY;
			return rc;
		}

		ret_fm = _filedebug_check_file(fname);
		if (ret_fm == 1)
			++count_fm;
		free(fname);
	}

	state->current_position.filemarks = count_fm;
	pos->filemarks = state->current_position.filemarks;

	if (state->p0_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_warning)
		pos->early_warning = true;
	else if (state->p1_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_warning)
		pos->early_warning = true;
	if (state->p0_p_warning && state->current_position.partition == 0 &&
		state->current_position.block >= state->p0_p_warning)
		pos->programmable_early_warning = true;
	else if (state->p1_p_warning && state->current_position.partition == 1 &&
		state->current_position.block >= state->p1_p_warning)
		pos->programmable_early_warning = true;
	return rc;
}

/**
 * NOTE: real tape drives erase from the current position. This function erases the entire
 * partition. The erase function is unused externally, but this implementation will need to be
 * fixed if it is ever needed.
 */
int filedebug_erase(void *vstate, struct tc_position *pos, bool long_erase)
{
	int ret;
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12196E");
		return -EDEV_NOT_READY;
	}

	ltfsmsg(LTFS_DEBUG, "12197D", (unsigned long)state->current_position.partition);
	pos->block     = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	ret = _filedebug_write_eod(state);
	return ret;
}

#define BARCODE_SIZE (36)

int filedebug_load(void *vstate, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	int ret;
	unsigned int i;
	uint64_t wp;
	char buf[BARCODE_SIZE], *dirlink, *config_file;
	struct stat d;

	if (state->ready) {
		state->current_position.partition = 0;
		state->current_position.block     = 0;
		state->current_position.filemarks = 0;
		state->write_counter              = 0;
		state->read_counter               = 0;
		return DEVICE_GOOD; /* already loaded the tape */
	}

	if (state->fd > 0) {
		memset(buf, 0, sizeof(buf));
		ret = lseek(state->fd, 0, SEEK_SET);
		if (ret < 0)
			return -EDEV_HARDWARE_ERROR;

		ret = read(state->fd, buf, sizeof(buf));
		if (ret != sizeof(buf)) {
			ltfsmsg(LTFS_ERR, "12264E", "");
			return -EDEV_HARDWARE_ERROR;
		}

		dirlink = buf;

		if(dirlink[strlen(dirlink) - 1] == '\n')
			dirlink[strlen(dirlink) - 1] = '\0';

		if(!strcmp(dirlink, "empty")) {
			ltfsmsg(LTFS_INFO, "12265I", "");
			return -EDEV_NO_MEDIUM;
		}

		if(state->dirname) {
			free(state->dirname);
			state->dirname = NULL;
		}

		ret = asprintf(&state->dirname, "%s/%s", state->dirbase, dirlink);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "10001E", "Directory name pointed by redirectiong file");
			return -EDEV_INTERNAL_ERROR;
		}

		/* make sure directory exists */
		ret = stat(state->dirname, &d);
		if (ret || !S_ISDIR(d.st_mode)) {
			ltfsmsg(LTFS_ERR, "12266E", state->dirname);
			return -EDEV_NO_MEDIUM;
		}
	}

	ltfsmsg(LTFS_INFO, "12262I", state->dirname);

	/* Load configuration of cartridge */
	ret = asprintf(&config_file, "%s/%s", state->dirname, CARTRIDGE_CONFIG );
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12255E", ret);
		return -EDEV_INTERNAL_ERROR;
	}

	ret = stat(config_file, &d);
	if (! ret && S_ISDIR(d.st_mode)) {
		ltfsmsg(LTFS_ERR, "12257E", ret);
		free(config_file);
		return -EDEV_INTERNAL_ERROR;
	}

	if (ret < 0 && errno == ENOENT)
		filedebug_conf_tc_write_xml(config_file, &state->conf);
	else if (! ret)
		filedebug_conf_tc_read_xml(config_file, &state->conf);
	else {
		ltfsmsg(LTFS_ERR, "12256E", ret);
		free(config_file);
		return -EDEV_INTERNAL_ERROR;
	}

	free(config_file);
	state->ready = true;

	for (i=0; i<MAX_PARTITIONS; ++i) {
		ret = filedebug_search_eod(state, i);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "12198E", i, ret);
			return -EDEV_INTERNAL_ERROR;
		}
	}

	if (state->conf.dummy_io)
		_filedebug_remove_record(state, 1, state->eod[1]);

	state->current_position.partition = 0;
	state->current_position.block     = 0;
	state->current_position.filemarks = 0;
	if (state->eod[1] == 0)
		state->partitions = 1;
	else
		state->partitions = MAX_PARTITIONS;

	pos->partition = state->current_position.partition;
	pos->block     = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	wp = 0;
	if(_get_wp(vstate, &wp) != 0) {
		ltfsmsg(LTFS_ERR, "12199E");
		return -EDEV_INTERNAL_ERROR;
	}

	state->write_pass_prev = wp;
	state->write_pass = wp;

	/* Calculate early warning threadolds */
	if (state->partitions == 2) {
		/* Assume 512KB per 1 record */
		state->p0_warning = calc_p0_cap(state) * 2;
		state->p1_warning = calc_p1_cap(state) * 2;
		state->p0_p_warning = state->p0_warning * 2;
		state->p1_p_warning = state->p1_warning - state->p0_p_warning;
	} else {
		state->p0_warning = calc_p0_cap(state) * 2;
		state->p1_warning = 0;
		state->p0_p_warning = state->p0_warning * 2;
		state->p1_p_warning = 0;
	}

	return DEVICE_GOOD;
}

int filedebug_unload(void *vstate, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	/* Write EOD of DP here when dummy io mode is enebaled */
	if (state->conf.dummy_io) {
		state->current_position.partition = 1;
		state->current_position.block = state->eod[1];
		_filedebug_write_eod(state);
	}

	state->ready = false;
	state->current_position.partition = 0;
	state->current_position.block     = 0;
	state->current_position.filemarks = 0;
	state->write_counter              = 0;
	state->read_counter               = 0;

	pos->partition = state->current_position.partition;
	pos->block     = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	return DEVICE_GOOD;
}

int filedebug_readpos(void *vstate, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12200E");
		return -EDEV_NOT_READY;
	}

	if (state->force_readperm) {
		state->read_counter++;
		if (state->read_counter > state->force_readperm) {
			ltfsmsg(LTFS_INFO, "12222I", "read");
			return -EDEV_READ_PERM;
		}
	}

	pos->partition = state->current_position.partition;
	pos->block     = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;

	ltfsmsg(LTFS_DEBUG, "12155D", "readpos", (unsigned long long)state->current_position.partition,
		(unsigned long long)state->current_position.block,
		(unsigned long long)state->current_position.filemarks);
	return DEVICE_GOOD;
}

int filedebug_setcap(void *vstate, uint16_t proportion)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	struct tc_position pos;

	if(state->current_position.partition != 0 ||
		state->current_position.block != 0)
	{
		ltfsmsg(LTFS_ERR, "12226E");
		return -EDEV_ILLEGAL_REQUEST;
	}

	state->partitions = 1;

	/* erase all partitions */
	state->current_position.partition = 1;
	state->current_position.block = 0;
	filedebug_erase(state, &pos, false);
	state->current_position.partition = 0;
	state->current_position.block = 0;
	filedebug_erase(state, &pos, false);

	return DEVICE_GOOD;
}

int filedebug_format(void *vstate, TC_FORMAT_TYPE format)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	struct tc_position pos;

	if(state->current_position.partition != 0 ||
		state->current_position.block != 0)
	{
		ltfsmsg(LTFS_ERR, "12201E");
		return -EDEV_ILLEGAL_REQUEST;
	}

	switch(format){
		case TC_FORMAT_DEFAULT:
			state->partitions = 1;
			break;
		case TC_FORMAT_PARTITION:
		case TC_FORMAT_DEST_PART:
			state->partitions = 2;
			break;
		default:
			ltfsmsg(LTFS_ERR, "12202E");
			return -EDEV_INVALID_ARG;
	}

	/* erase all partitions */
	state->current_position.partition = 1;
	state->current_position.block = 0;
	filedebug_erase(state, &pos, false);
	state->current_position.partition = 0;
	state->current_position.block = 0;
	filedebug_erase(state, &pos, false);

	/* Calculate early warning threadolds */
	if (state->partitions == 2) {
		/* Assume 512KB per 1 record */
		state->p0_warning = calc_p0_cap(state) * 2;
		state->p1_warning = calc_p1_cap(state) * 2;
		state->p0_p_warning = state->p0_warning * 2;
		state->p1_p_warning = state->p1_warning - state->p0_p_warning;
	} else {
		state->p0_warning = calc_p0_cap(state) * 2;
		state->p1_warning = 0;
		state->p0_p_warning = state->p0_warning * 2;
		state->p1_p_warning = 0;
	}

	return DEVICE_GOOD;
}

int filedebug_remaining_capacity(void *vstate, struct tc_remaining_cap *cap)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12203E");
		return DEVICE_GOOD;
	}

	if(state->partitions == 2) {
		cap->max_p0       = calc_p0_cap(state);
		cap->remaining_p0 = calc_p0_remaining(state);
		cap->max_p1       = calc_p1_cap(state);
		cap->remaining_p1 = calc_p1_remaining(state);
	} else {
		cap->max_p0       = state->conf.capacity_mb;
		cap->remaining_p0 = 0;
		cap->max_p1       = 0;
		cap->remaining_p1 = 0;
	}

	return DEVICE_GOOD;
}

int filedebug_get_cartridge_health(void *device, struct tc_cartridge_health *cart_health)
{
	cart_health->mounts           = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_ds       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_temps      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_ds          = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_temps       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms       = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->write_perms_prev = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_perms_prev  = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->written_mbytes   = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->read_mbytes      = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_begin     = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->passes_middle    = UNSUPPORTED_CARTRIDGE_HEALTH;
	cart_health->tape_efficiency  = UNSUPPORTED_CARTRIDGE_HEALTH;

	return DEVICE_GOOD;
}

int filedebug_get_tape_alert(void *device, uint64_t *tape_alert)
{
	*tape_alert = 0;
	return DEVICE_GOOD;
}

int filedebug_clear_tape_alert(void *device, uint64_t tape_alert)
{
	return DEVICE_GOOD;
}

int filedebug_get_xattr(void *device, const char *name, char **buf)
{
	return -LTFS_NO_XATTR;
}

int filedebug_set_xattr(void *device, const char *name, const char *buf, size_t size)
{
	struct filedebug_data *state = (struct filedebug_data *) device;
	int rc = -LTFS_NO_XATTR;

	if (! strcmp(name, "ltfs.vendor.IBM.forceErrorWrite")) {
		state->force_writeperm = strtoull(buf, NULL, 0);
		if (state->force_writeperm && state->force_writeperm < THREASHOLD_FORCE_WRITE_NO_WRITE)
			state->force_writeperm = THREASHOLD_FORCE_WRITE_NO_WRITE;
		rc = DEVICE_GOOD;
	} else if (! strcmp(name, "ltfs.vendor.IBM.forceErrorRead")) {
		state->force_readperm = strtoull(buf, NULL, 0);
		state->read_counter = 0;
		rc = DEVICE_GOOD;
	}

	return rc;
}

int filedebug_logsense(void *device, const uint8_t page, unsigned char *buf, const size_t size)
{
	ltfsmsg(LTFS_ERR, "10007E", __FUNCTION__);
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int filedebug_modesense(void *device, const uint8_t page, const TC_MP_PC_TYPE pc, const uint8_t subpage, unsigned char *buf, const size_t size)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	memset(buf, 0, size);

	buf[16] = page;

	/* Return density code or cart type, if specific value is set */
	if (page == TC_MP_SUPPORTEDPAGE && pc == TC_MP_PC_CURRENT && subpage == 0x00)
		buf[8] = state->conf.density_code;
	else if (page == TC_MP_MEDIUM_PARTITION && pc == TC_MP_PC_CURRENT && subpage == 0x00)
		buf[2] = state->conf.cart_type;

	return DEVICE_GOOD;
}

int filedebug_modeselect(void *device, unsigned char *buf, const size_t size)
{
	struct filedebug_data *state = (struct filedebug_data *)device;

	if (buf[16]==TC_MP_READ_WRITE_CTRL && buf[26]!=0) {
		/* Update density code, if specific value is set */
		state->conf.density_code = buf[26];
	}

	return DEVICE_GOOD;
}

int filedebug_reserve_unit(void *vstate)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	if (state->device_reserved) {
		ltfsmsg(LTFS_ERR, "12204E");
		return -EDEV_ILLEGAL_REQUEST;
	}
	state->device_reserved = true;
	return DEVICE_GOOD;
}

int filedebug_release_unit(void *vstate)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	state->device_reserved = false;
	return DEVICE_GOOD;
}

int filedebug_prevent_medium_removal(void *vstate)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12205E");
		return -EDEV_NOT_READY;
	}
	state->medium_locked = true; /* TODO: fail if medium is already locked? */
	return DEVICE_GOOD;
}

int filedebug_allow_medium_removal(void *vstate)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12206E");
		return -EDEV_NOT_READY;
	}
	state->medium_locked = false;
	return DEVICE_GOOD;
}

int filedebug_read_attribute(void *vstate, const tape_partition_t part, const uint16_t id
							 , unsigned char *buf, const size_t size)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	char *fname;
	int fd;
	ssize_t bytes_read;

	ltfsmsg(LTFS_DEBUG, "12152D", "readattr", (unsigned long)part, id);

	/* Open attribute record */
	fname = _filedebug_make_attrname(state, part, id);
	if (!fname)
		return -EDEV_NO_MEMORY;
	fd = open(fname, O_RDONLY | O_BINARY);
	free(fname);
	if (fd < 0) {
		ltfsmsg(LTFS_WARN, "12207W", errno);
		return -EDEV_CM_PERM;
	}

	/* TODO: return -EDEV_INVALID_ARG if buffer is too small to hold complete record? */
	bytes_read = read(fd, buf, size);
	if(bytes_read == -1) {
		ltfsmsg(LTFS_WARN, "12208W", errno);
		close(fd);
		return -EDEV_CM_PERM;
	}
	close(fd);

	return DEVICE_GOOD;
}

int filedebug_write_attribute(void *vstate, const tape_partition_t part
							  , const unsigned char *buf, const size_t size)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	char *fname;
	int fd;
	ssize_t written;
	uint16_t id, attr_size;
	size_t i = 0;

	while(size > i)
	{
		id = ltfs_betou16(buf + i);
		attr_size = ltfs_betou16(buf + (i + 3));

		ltfsmsg(LTFS_DEBUG, "12209D", id, attr_size);

		/* Create attribute record */
		fname = _filedebug_make_attrname(state, part, id);
		if (!fname) {
			ltfsmsg(LTFS_ERR, "12210E");
			return -EDEV_NO_MEMORY;
		}
		fd = open(fname,
				  O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
				  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
		free(fname);
		if (fd < 0) {
			ltfsmsg(LTFS_ERR, "12158E", errno);
			return -EDEV_CM_PERM;
		}

		/* write and close the file */
		written = write(fd, buf, size);
		if (written < 0) {
			ltfsmsg(LTFS_ERR, "12159E", errno);
			close(fd);
			return -EDEV_CM_PERM;
		}
		close(fd);

		i += (attr_size + 5); /* Add header size of an attribute */
	}

	return DEVICE_GOOD;
}

int filedebug_allow_overwrite(void *device, const struct tc_position pos)
{
	return DEVICE_GOOD;
}

int filedebug_report_density(void *device, struct tc_density_report *rep, bool medium)
{
	/* Always return LTO5 */
	rep->size = 1;
	rep->density[0].primary   = TC_DC_LTO5;
	rep->density[0].secondary = TC_DC_LTO5;
	return DEVICE_GOOD;
}

int filedebug_get_eod_status(void *vstate, int partition)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	if(state->eod[partition] == MISSING_EOD)
		return EOD_MISSING;
	else
		return EOD_GOOD;
}

int filedebug_set_compression(void *vstate, bool enable_compression, struct tc_position *pos)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;
	if (!state->ready) {
		ltfsmsg(LTFS_ERR, "12211E");
		return -EDEV_NOT_READY;
	}
	pos->block = state->current_position.block;
	pos->filemarks = state->current_position.filemarks;
	return DEVICE_GOOD;
}

int filedebug_set_default(void *device)
{
	return DEVICE_GOOD;
}

int filedebug_get_parameters(void *vstate, struct tc_drive_param *drive_param)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	drive_param->max_blksize = FILE_DEBUG_MAX_BLOCK_SIZE;
	drive_param->logical_write_protect = false;
	drive_param->write_protect = state->conf.emulate_readonly;

	return DEVICE_GOOD;
}

const char *filedebug_default_device_name(void)
{
	return filedebug_default_device;
}

/**
 * examine given directory to find EOD for a partition.
 * returns 0 on success, negative value on error
 * on success, sets the tape position to EOD on the given partition.
 */
int filedebug_search_eod(struct filedebug_data *state, int partition)
{
	char *fname;
	size_t fname_len;
	int ret;
	int i;
	int f[3] = { 1, 1, 0 };
	DIR *dp;
	int p;
	tape_block_t b;
	struct dirent *entry;

	state->current_position.partition = partition;
	state->current_position.block     = 0;

	/* loop until an EOD mark is found or no record is found */
	while ((f[0] || f[1]) && !f[2]) {
		/* check for a record */
		fname = _filedebug_make_current_filename(state, '.');
		if (!fname) {
			ltfsmsg(LTFS_ERR, "12213E");
			return -EDEV_NO_MEMORY;
		}
		fname_len = strlen(fname);

		for (i=0; i<3; ++i) {
			fname[fname_len-1] = rec_suffixes[i];
			f[i] = _filedebug_check_file(fname);
			if (f[i] < 0) {
				ltfsmsg(LTFS_ERR, "12214E", f[i]);
				free(fname);
				return f[i];
			}
		}

		free(fname);
		++state->current_position.block;
	}
	--state->current_position.block;

	if(!f[2] && state->current_position.block != 0) {
		state->last_block[state->current_position.partition] = state->current_position.block;
		state->eod[state->current_position.partition] = MISSING_EOD;
		if (state->conf.dummy_io) {
			dp = opendir(state->dirname);
			if (! dp) {
				ltfsmsg(LTFS_ERR, "12270E", state->dirname);
				return 0;
			}

			while ((entry = readdir(dp))) {
				if ( entry->d_name[strlen(entry->d_name) - 1] == 'E') {
					entry->d_name[strlen(entry->d_name) - 2] = '\0';
					entry->d_name[1] = '\0';
					p = atoi(entry->d_name);
					b = atoll(&entry->d_name[2]);
					if (p == partition) {
						state->current_position.block = state->last_block[partition] = --b;
						state->eod[partition] = 0;
						ret = _filedebug_write_eod(state);
						if (ret < 0) {
							ltfsmsg(LTFS_ERR, "12215E", ret);
							closedir(dp);
							return ret;
						}
						break;
					}
				}
			}
			closedir(dp);
		}
	} else {
		ret = _filedebug_write_eod(state);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "12215E", ret);
			return ret;
		}
	}

	return DEVICE_GOOD;
}

/**
 * Write an EOD mark at the current tape position, remove extra records, and
 * update the EOD in the state variable.
 * Returns 0 on success, negative value on failure.
 */
int _filedebug_write_eod(struct filedebug_data *state)
{
	char *fname;
	int fd;
	int ret;
	uint64_t i;
	bool remove_extra_rec = true;

	if(state->eod[state->current_position.partition] == MISSING_EOD)
		remove_extra_rec = false;

	/* remove any existing record at this position */
	ret = _filedebug_remove_current_record(state);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12216E", ret);
		return ret;
	}

	/* create EOD record */
	fname = _filedebug_make_current_filename(state, 'E');
	if (!fname) {
		ltfsmsg(LTFS_ERR, "12217E");
		return -EDEV_NO_MEMORY;
	}
	fd = open(fname,
			  O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
			  S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	free(fname);
	if (fd < 0 || close(fd) < 0) {
		ltfsmsg(LTFS_ERR, "12218E", errno);
		return -EDEV_RW_PERM;
	}

	if(remove_extra_rec) {
		/* remove records following this position */
		for (i=state->current_position.block+1; i<=state->eod[state->current_position.partition]; ++i) {
			ret = _filedebug_remove_record(state, state->current_position.partition, i);
			if (ret < 0) {
				ltfsmsg(LTFS_ERR, "12219E", ret);
				return ret;
			}
		}
	}

	state->last_block[state->current_position.partition] = state->current_position.block - 1;
	state->eod[state->current_position.partition] = state->current_position.block;
	return DEVICE_GOOD;
}

/**
 * Delete the file associated with the current tape position.
 */
int _filedebug_remove_current_record(const struct filedebug_data *state)
{
	return _filedebug_remove_record(state
									, state->current_position.partition
									, state->current_position.block);
}

/**
 * Delete the file associated with a given tape position.
 * @return 1 on successful delete, 0 if no file found, negative on error.
 */
int _filedebug_remove_record(const struct filedebug_data *state,
	int partition, uint64_t blknum)
{
	char *fname;
	size_t fname_len;
	int i;
	int ret;

	fname = _filedebug_make_filename(state, partition, blknum, '.');
	if (!fname) {
		ltfsmsg(LTFS_ERR, "12220E");
		return -EDEV_NO_MEMORY;
	}
	fname_len = strlen(fname);

	for (i=0; i<3; ++i) {
		fname[fname_len-1] = rec_suffixes[i];
		ret = unlink(fname);
		if (ret < 0 && errno != ENOENT) {
			ltfsmsg(LTFS_ERR, "12221E", errno);
			free(fname);
			return -EDEV_RW_PERM;
		}
	}

	free(fname);
	return DEVICE_GOOD;
}

/**
 * Check for the existence and writability of a file.
 * This function is silent: callers are expected to report errors for themselves.
 * @return 1 on success, 0 if file does not exist, and -errno on error
 */
int _filedebug_check_file(const char *fname)
{
	int fd;
	int ret;

	fd = open(fname, O_RDWR | O_BINARY);
	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		else
			return -EDEV_RW_PERM;
	} else {
		ret = close(fd);
		if (ret < 0)
			return -EDEV_RW_PERM;
		else
			return 1;
	}
}

/**
 * Call _filedebug_make_filename with the current tape position
 */
char *_filedebug_make_current_filename(const struct filedebug_data *state, char type)
{
	return _filedebug_make_filename(state
									, state->current_position.partition
									, state->current_position.block
									, type);
}

/**
 * Make filename for a record.
 * Returns a string on success or NULL on failure. The caller is responsible for freeing
 * the returned memory. Failure probably means asprintf couldn't allocate memory.
 */
char *_filedebug_make_filename(const struct filedebug_data *state,
	int part, uint64_t pos, char type)
{
	char *fname;
	int ret;
	ret = asprintf(&fname, "%s/%d_%"PRIu64"_%c", state->dirname, part, pos, type);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return NULL;
	}
	return fname;
}

/**
 * Make filename for a Attribute.
 * Returns a string on success or NULL on failure. The caller is responsible for freeing
 * the returned memory. Failure probably means asprintf couldn't allocate memory.
 */
char *_filedebug_make_attrname(const struct filedebug_data *state, int part, int id)
{
	char *fname;
	int ret;
	ret = asprintf(&fname, "%s/attr_%d_%x", state->dirname, part, id);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "10001E", __FUNCTION__);
		return NULL;
	}
	return fname;
}

/**
 * Space over filemarks. Position immediately after the FM if spacing forwards, or
 * immediately before it if spacing backwards.
 * @param state the tape state
 * @param count number of filemarks to skip
 * @param back true to skip backwards, false to skip forwards
 * @return 0 on success or a negative value on error
 */
int _filedebug_space_fm(struct filedebug_data *state, uint64_t count, bool back)
{
	char *fname;
	uint64_t fm_count = 0;
	int ret;

	if (count == 0)
		return DEVICE_GOOD;

	if (back && state->current_position.block > 0)
		--state->current_position.block;

	while (1) {
		if (!back &&
			state->current_position.block == state->eod[state->current_position.partition]) {
			ltfsmsg(LTFS_ERR, "12222E");
			return -EDEV_EOD_DETECTED;
		}

		if (!back &&
			state->current_position.block == state->last_block[state->current_position.partition] + 1) {
			return -EDEV_RW_PERM;
		}

		fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_FILEMARK]);
		if (!fname) {
			ltfsmsg(LTFS_ERR, "12223E");
			return -EDEV_NO_MEMORY;
		}
		ret = _filedebug_check_file(fname);
		free(fname);
		if (ret < 0) {
			ltfsmsg(LTFS_ERR, "12224E", ret);
			return ret;
		} else if (ret > 0) {
			++fm_count;
			if (fm_count == count) {
				if (!back)
					++state->current_position.block;
				return DEVICE_GOOD;
			}
		}

		if (back) {
			if (state->current_position.block == 0) {
				ltfsmsg(LTFS_ERR, "12225E");
				return -EDEV_BOP_DETECTED;
			}
			--state->current_position.block;
		} else {
			++state->current_position.block;
		}
	}
}

/**
 * Space over records. If FM is encountered, position immediately after it if spacing forwards
 * or immediately before it if spacing backwards.
 * TODO returns -EIO if it encounters a FM or BOT/EOD. fix for correct behavior if needed.
 * TODO: add proper error reporting if this function is ever needed
 * NOTE: this function is not used for anything. It may or may not behave as advertised.
 * @param state the tape state
 * @param count number of records to skip
 * @param back true to skip backwards, false to skip forwards
 * @return 0 on success or a negative value on error
 */
int _filedebug_space_rec(struct filedebug_data *state, uint64_t count, bool back)
{
	char *fname;
	uint64_t rec_count = 0;
	int ret;

	if (count == 0)
		return DEVICE_GOOD;

	while (1) {
		if (!back &&
			state->current_position.block == state->eod[state->current_position.partition]) {
			return -EDEV_EOD_DETECTED;
		}

		if (!back &&
			state->current_position.block == state->last_block[state->current_position.partition] + 1) {
			return -EDEV_RW_PERM;
		}

		/* check for filemark */
		fname = _filedebug_make_current_filename(state, rec_suffixes[SUFFIX_FILEMARK]);
		if (!fname)
			return -EDEV_NO_MEMORY;
		ret = _filedebug_check_file(fname);
		free(fname);
		if (ret < 0)
			return ret;
		if (ret > 0 && (!back || rec_count > 0)) {
			if (!back)
				++state->current_position.block;
			return -EDEV_RW_PERM;
		}

		if (back) {
			if (state->current_position.block == 0) {
				return -EDEV_BOP_DETECTED;
			}
			--state->current_position.block;
		} else {
			++state->current_position.block;
		}

		++rec_count;
		if (rec_count == count) {
			return DEVICE_GOOD;
		}
	}
}

int _get_wp(struct filedebug_data *vstate, uint64_t *wp)
{
	int ret;
	unsigned char wp_data[TC_MAM_PAGE_VCR_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	memset(wp_data, 0, sizeof(wp_data));

	*wp = 0;
	ret = filedebug_read_attribute(vstate, 0, TC_MAM_PAGE_VCR
								   , wp_data, sizeof(wp_data));
	if(ret == 0)
		*wp = ltfs_betou32(wp_data + 5);
	else
		ret = _set_wp(vstate, (uint64_t)1);

	return ret;
}

int _set_wp(struct filedebug_data *vstate, uint64_t wp)
{
	int ret;
	unsigned char wp_data[TC_MAM_PAGE_VCR_SIZE + TC_MAM_PAGE_HEADER_SIZE];

	ltfs_u16tobe(wp_data, TC_MAM_PAGE_VCR);
	wp_data[2] = 0;
	ltfs_u16tobe(wp_data + 3, TC_MAM_PAGE_VCR_SIZE);
	ltfs_u32tobe(wp_data + 5, (uint32_t)wp);

	ret = filedebug_write_attribute(vstate, 0, wp_data, sizeof(wp_data));

	return ret;
}

/**
 * Get valid device list. Returns an empty list because there's no way to enumerate
 * all the possible valid devices for this backend.
 */
#define DRIVE_FILE_PREFIX "Drive_"

int filedebug_get_device_list(struct tc_drive_info *buf, int count)
{
	char *filename, *devdir, line[1024];
	FILE *infile;
	DIR *dp;
	struct dirent *entry;
	int deventries = 0;
	char *ser= NULL, *pid = NULL, *tmp;
	int i, len;

	if (! original_pid) {
		original_pid = (long)getpid();
	}

	/* Create a file to indicate current directory of drive link (for tape file backend) */
	asprintf(&filename, "%s/ltfs%ld", DRIVE_LIST_DIR, original_pid);
	if (!filename) {
		ltfsmsg(LTFS_ERR, "10001E", "filechanger_data drive file name");
		return -LTFS_NO_MEMORY;
	}
	ltfsmsg(LTFS_INFO, "12267I", filename);
	infile = fopen(filename, "r");
	if (!infile) {
		ltfsmsg(LTFS_INFO, "12268I", filename);
		return 0;
	} else {
		devdir = fgets(line, sizeof(line), infile);
		if(devdir[strlen(devdir) - 1] == '\n')
			devdir[strlen(devdir) - 1] = '\0';
		fclose(infile);
		free(filename);
	}

	ltfsmsg(LTFS_INFO, "12269I", devdir);
	dp = opendir(devdir);
	if (! dp) {
		ltfsmsg(LTFS_ERR, "12270E", devdir);
		return 0;
	}

	while ((entry = readdir(dp))) {
		if (strncmp(entry->d_name, DRIVE_FILE_PREFIX, strlen(DRIVE_FILE_PREFIX)))
			continue;

		if (buf && deventries < count) {
			tmp = strdup(entry->d_name);
			if (! *tmp) {
				ltfsmsg(LTFS_ERR, "10001E", "filedebug_get_device_list");
				return -ENOMEM;
			}

			for (i = strlen(tmp) - 1; i > 0; --i) {
				if (tmp[i] == '.') {
					tmp[i] = '\0';
					pid = &tmp[i + 1];
				}
				if (tmp[i] == '_') {
					tmp[i] = '\0';
					ser = &tmp[i + 1];
					break;
				}
			}

			snprintf(buf[deventries].name, TAPE_DEVNAME_LEN_MAX, "%s/%s", devdir, entry->d_name);
			snprintf(buf[deventries].vendor, TAPE_VENDOR_NAME_LEN_MAX, "DUMMY");
			snprintf(buf[deventries].model, TAPE_MODEL_NAME_LEN_MAX, "%s", pid);
			snprintf(buf[deventries].serial_number, TAPE_SERIAL_LEN_MAX, "%s", ser);
			len = snprintf(buf[deventries].product_name, PRODUCT_NAME_LENGTH, " [%s]", pid);
			/* Fit to the length defined as *supported_devices[] into ibmtape_cmn.c for identifier compatibility */
			if (len < PRODUCT_NAME_REPORT_LENGTH) {
				for (i = len; i < PRODUCT_NAME_REPORT_LENGTH; i++)
					buf[deventries].product_name[i] = ' ';
				buf[deventries].product_name[PRODUCT_NAME_REPORT_LENGTH] = '\0';
			}
			ltfsmsg(LTFS_DEBUG, "12271D", buf[deventries].name, buf[deventries].vendor,
					buf[deventries].model, buf[deventries].serial_number);

			free(tmp);
		}

		deventries++;
	}

	closedir(dp);

	return deventries;
}

int filedebug_set_key(void *device, const unsigned char *keyalias, const unsigned char *key)
{
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int filedebug_get_keyalias(void *device, unsigned char **keyalias)
{
	return -EDEV_UNSUPPORTED_FUNCTION;
}

int filedebug_takedump_drive(void *device, bool nonforced_dump)
{
	/* Do nothing */
	return DEVICE_GOOD;
}

bool filedebug_is_mountable(void *device, const char *barcode, const unsigned char density_code)
{
	/* Do nothing */
	return true;
}

int filedebug_get_worm_status(void *device, bool *is_worm)
{
	*is_worm = false;
	return DEVICE_GOOD;
}

int filedebug_get_serialnumber(void *vstate, char **result)
{
	struct filedebug_data *state = (struct filedebug_data *)vstate;

	CHECK_ARG_NULL(vstate, -LTFS_NULL_ARG);
	CHECK_ARG_NULL(result, -LTFS_NULL_ARG);

	if (state->serial_number)
		*result = strdup((const char *) state->serial_number);
	else
		*result = strdup("DUMMY");

	if (! *result)
		return -EDEV_NO_MEMORY;

	return DEVICE_GOOD;
}

struct tape_ops filedebug_handler = {
	.open                   = filedebug_open,
	.reopen                 = filedebug_reopen,
	.close                  = filedebug_close,
	.close_raw              = filedebug_close_raw,
	.is_connected           = filedebug_is_connected,
	.inquiry                = filedebug_inquiry,
	.inquiry_page           = filedebug_inquiry_page,
	.test_unit_ready        = filedebug_test_unit_ready,
	.read                   = filedebug_read,
	.write                  = filedebug_write,
	.writefm                = filedebug_writefm,
	.rewind                 = filedebug_rewind,
	.locate                 = filedebug_locate,
	.space                  = filedebug_space,
	.erase                  = filedebug_erase,
	.load                   = filedebug_load,
	.unload                 = filedebug_unload,
	.readpos                = filedebug_readpos,
	.setcap                 = filedebug_setcap,
	.format                 = filedebug_format,
	.remaining_capacity     = filedebug_remaining_capacity,
	.logsense               = filedebug_logsense,
	.modesense              = filedebug_modesense,
	.modeselect             = filedebug_modeselect,
	.reserve_unit           = filedebug_reserve_unit,
	.release_unit           = filedebug_release_unit,
	.prevent_medium_removal = filedebug_prevent_medium_removal,
	.allow_medium_removal   = filedebug_allow_medium_removal,
	.write_attribute        = filedebug_write_attribute,
	.read_attribute         = filedebug_read_attribute,
	.allow_overwrite        = filedebug_allow_overwrite,
	.report_density         = filedebug_report_density,
	.set_compression        = filedebug_set_compression,
	.set_default            = filedebug_set_default,
	.get_cartridge_health   = filedebug_get_cartridge_health,
	.get_tape_alert         = filedebug_get_tape_alert,
	.clear_tape_alert       = filedebug_clear_tape_alert,
	.get_xattr              = filedebug_get_xattr,
	.set_xattr              = filedebug_set_xattr,
	.get_parameters         = filedebug_get_parameters,
	.get_eod_status         = filedebug_get_eod_status,
	.get_device_list        = filedebug_get_device_list,
	.help_message           = filedebug_help_message,
	.parse_opts             = filedebug_parse_opts,
	.default_device_name    = filedebug_default_device_name,
	.set_key                = filedebug_set_key,
	.get_keyalias           = filedebug_get_keyalias,
	.takedump_drive         = filedebug_takedump_drive,
	.is_mountable           = filedebug_is_mountable,
	.get_worm_status        = filedebug_get_worm_status,
	.get_serialnumber       = filedebug_get_serialnumber,
};

struct tape_ops *tape_dev_get_ops(void)
{
	return &filedebug_handler;
}

#ifndef mingw_PLATFORM
extern char driver_generic_file_dat[];
#endif

const char *tape_dev_get_message_bundle_name(void **message_data)
{
#ifndef mingw_PLATFORM
	*message_data = driver_generic_file_dat;
#else
	*message_data = NULL;
#endif
	return "driver_generic_file";
}
