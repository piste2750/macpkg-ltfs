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
** FILE NAME:       tape_drivers/tape_drivers.h
**
** DESCRIPTION:     Prototypes for common tape operations.
**
** AUTHOR:          Yutaka Oishi
**                  IBM Yamato, Japan
**                  OISHI@jp.ibm.com
**
*************************************************************************************
*/

#ifndef __tape_drivers_h
#define __tape_drivers_h

#define KB   (1024)
#define MB   (KB * 1024)
#define GB   (MB * 1024)

#define VENDOR_ID_LENGTH    8
#define PRODUCT_ID_LENGTH   16
#define PRODUCT_NAME_LENGTH (PRODUCT_ID_LENGTH + 3) /* " [PRODUCT_ID]" */
#define PRODUCT_NAME_REPORT_LENGTH 15
#define PRODUCT_REV_LENGTH  4

static const char base_firmware_level_lto5[] = "B170";
static const char base_firmware_level_ts1140[] = "3694";

typedef void  (*crc_enc)(void *buf, size_t n);
typedef int   (*crc_check)(void *buf, size_t n);
typedef void* (*memcpy_crc_enc)(void *dest, const void *src, size_t n);
typedef int   (*memcpy_crc_check)(void *dest, const void *src, size_t n);

typedef enum {
	DRIVE_UNSUPPORTED, /* Unsupported drive */
	DRIVE_LTO5,        /* IBM Ultrium Gen 5 */
	DRIVE_LTO5_HH,     /* IBM Ultrium Gen 5 Half-High */
	DRIVE_LTO6,        /* IBM Ultrium Gen 6 */
	DRIVE_LTO6_HH,     /* IBM Ultrium Gen 6 Half-High */
	DRIVE_LTO7,        /* IBM Ultrium Gen 7 */
	DRIVE_LTO7_HH,     /* IBM Ultrium Gen 7 Half-High */
	DRIVE_TS1140,      /* TS1140 */
	DRIVE_TS1150,      /* TS1150 */
} DRIVE_TYPE;

enum {
	TAPE_RANGE_UNKNOWN = 0,
	TAPE_RANGE_ENTERPRISE = 1,
	TAPE_RANGE_MIDDLE = 2,
};

enum {
	DRIVE_GEN_UNKNOWN = 0,
	DRIVE_GEN_LTO5 = 5,
	DRIVE_GEN_LTO6 = 6,
	DRIVE_GEN_LTO7 = 7,
	DRIVE_GEN_JAG4 = 0x104,
	DRIVE_GEN_JAG5 = 0x105,
};

typedef struct {
	DRIVE_TYPE drive_type;
	int drive_range;
	int drive_generation;
} DRIVE_TYPE_MAP;

typedef struct {
	char barcode_suffix[3];
	int density_code;
} BARCODE_DENSITY_MAP;

typedef struct {
	int drive_generation;
	int density_code;
} DRIVE_DENSITY_SUPPORT_MAP;

/* Cartridge type in mode page header */
enum {
	TC_MP_LTO1D_CART  = 0x18,   /* LTO1 Data cartridge */
	TC_MP_LTO2D_CART  = 0x28,   /* LTO2 Data cartridge */
	TC_MP_LTO3D_CART  = 0x38,   /* LTO3 Data cartridge */
	TC_MP_LTO4D_CART  = 0x48,   /* LTO4 Data cartridge */
	TC_MP_LTO5D_CART  = 0x58,   /* LTO5 Data cartridge */
	TC_MP_LTO6D_CART  = 0x68,   /* LTO6 Data cartridge */
	TC_MP_LTO7D_CART  = 0x78,   /* LTO7 Data cartridge */
	TC_MP_LTO3W_CART  = 0x3C,   /* LTO3 WORM cartridge */
	TC_MP_LTO4W_CART  = 0x4C,   /* LTO4 WORM cartridge */
	TC_MP_LTO5W_CART  = 0x5C,   /* LTO5 WORM cartridge */
	TC_MP_LTO6W_CART  = 0x6C,   /* LTO6 WORM cartridge */
	TC_MP_LTO7W_CART  = 0x7C,   /* LTO6 WORM cartridge */
	TC_MP_JA          = 0x91,   /* Jaguar JA cartridge */
	TC_MP_JW          = 0xA1,   /* Jaguar JW cartridge */
	TC_MP_JJ          = 0xB1,   /* Jaguar JJ cartridge */
	TC_MP_JR          = 0xC1,   /* Jaguar JR cartridge */
	TC_MP_JB          = 0x92,   /* Jaguar JB cartridge */
	TC_MP_JX          = 0xA2,   /* Jaguar JX cartridge */
	TC_MP_JK          = 0xB2,   /* Jaguar JK cartridge */
	TC_MP_JC          = 0x93,   /* Jaguar JC cartridge */
	TC_MP_JY          = 0xA3,   /* Jaguar JY cartridge */
	TC_MP_JL          = 0xB3,   /* Jaguar JL cartridge */
	TC_MP_JD          = 0x94,   /* Jaguar JD cartridge */
	TC_MP_JZ          = 0xA4,   /* Jaguar JZ cartridge */
};

#ifdef TAPE_BACKEND

static DRIVE_TYPE_MAP drive_type_map[] = {
	{ DRIVE_LTO5, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO5 },
	{ DRIVE_LTO5_HH, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO5 },
	{ DRIVE_LTO6, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO6 },
	{ DRIVE_LTO6_HH, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO6 },
	{ DRIVE_LTO7, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO7 },
	{ DRIVE_LTO7_HH, TAPE_RANGE_MIDDLE, DRIVE_GEN_LTO7 },
	{ DRIVE_TS1140, TAPE_RANGE_ENTERPRISE, DRIVE_GEN_JAG4 },
	{ DRIVE_TS1150, TAPE_RANGE_ENTERPRISE, DRIVE_GEN_JAG5 },
};

static DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density[] = {
	// Jaguar 4 drive supports only density code 54h medium
	{ DRIVE_GEN_JAG4, 0x54 },
	{ DRIVE_GEN_JAG5, 0x54 },
	{ DRIVE_GEN_JAG5, 0x55 },
};

static DRIVE_DENSITY_SUPPORT_MAP jaguar_drive_density_strict[] = {
		// Jaguar 4 drive supports only density code 54h medium
	{ DRIVE_GEN_JAG4, 0x54 },
	{ DRIVE_GEN_JAG5, 0x55 },
};

static BARCODE_DENSITY_MAP lto_barcode_density[] = {
	{ "L5", 0x58 },
	{ "L6", 0x5A },
	{ "L7", 0x5C },
};

static DRIVE_DENSITY_SUPPORT_MAP lto_drive_density[] = {
	// LTO5 drive supports only LTO5 medium
	{ DRIVE_GEN_LTO5, 0x58 },
	// LTO6 drive supports both LTO5 and LTO6 media
	{ DRIVE_GEN_LTO6, 0x58 },
	{ DRIVE_GEN_LTO6, 0x5A },
	// LTO7 drive supports both LTO5 and LTO6 media
	{ DRIVE_GEN_LTO7, 0x5A },
	{ DRIVE_GEN_LTO7, 0x5C },
};

static DRIVE_DENSITY_SUPPORT_MAP lto_drive_density_strict[] = {
	// LTO5 drive supports only LTO5 medium
	{ DRIVE_GEN_LTO5, 0x58 },
	// LTO6 drive supports only LTO6 medium for strict mode
	{ DRIVE_GEN_LTO6, 0x5A },
	// LTO7 drive supports only LTO6 medium for strict mode
	{ DRIVE_GEN_LTO7, 0x5C },
};

static int supported_cart[] = {
	TC_MP_LTO7D_CART,
	TC_MP_LTO6D_CART,
	TC_MP_LTO5D_CART,
	TC_MP_JB,
	TC_MP_JC,
	TC_MP_JD,
	TC_MP_JK,
	TC_MP_JY,
	TC_MP_JL,
	TC_MP_JZ
};

static int num_drive_type_map = sizeof(drive_type_map) / sizeof(DRIVE_TYPE_MAP);
static int num_jaguar_drive_density = sizeof(jaguar_drive_density) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);
static int num_jaguar_drive_density_strict = sizeof(jaguar_drive_density_strict) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);
static int num_lto_barcode_density = sizeof(lto_barcode_density) / sizeof(BARCODE_DENSITY_MAP);
static int num_lto_drive_density = sizeof(lto_drive_density) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);
static int num_lto_drive_density_strict = sizeof(lto_drive_density_strict) / sizeof(DRIVE_DENSITY_SUPPORT_MAP);
#endif // TAPE_BACKEND

#endif // __tape_drivers_h
