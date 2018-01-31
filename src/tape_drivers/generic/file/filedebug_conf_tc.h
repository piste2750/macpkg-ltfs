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
** FILE NAME:       filedebug_conf_tc.h
**
** DESCRIPTION:     Header file for the XML parser for changer file backend
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/
#ifndef __filedebug_conf_tc_h
#define __filedebug_conf_tc_h

#include <libltfs/xml.h>

struct filedebug_conf_tc
{
	bool      dummy_io;         /**< Dummy IO mode to evaliate upper layer performance */
	bool      emulate_readonly; /**< True to emulate a cartridge in read-only mode */
	uint64_t  capacity_mb;      /**< Configure cartridge capacity */
	char      cart_type;        /**< Cartridge type defined in tape_drivers.h */
	char      density_code;     /**< Density code */
};

struct filedebug_tc_cart_type {
	char *name;
	char type_code;
};

extern struct filedebug_tc_cart_type cart_type[];
extern int cart_type_size;

int filedebug_conf_tc_write_xml(char *filename, const struct filedebug_conf_tc *conf);
int filedebug_conf_tc_read_xml(char *filename, struct filedebug_conf_tc *conf);


#endif /* __filedebug_conf_tc_h */
