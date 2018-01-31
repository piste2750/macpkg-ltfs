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
** FILE NAME:       filedebug_conf_tc.c
**
** DESCRIPTION:     XML parser for tape file backend configuration
**
** AUTHOR:          Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/

#include "libltfs/ltfs.h"
#include "libltfs/ltfs_error.h"
#include "libltfs/ltfslogging.h"

#include "filedebug_conf_tc.h"

struct filedebug_tc_cart_type cart_type[]  =
{
	{ "L5", TC_MP_LTO5D_CART },
	{ "L6", TC_MP_LTO6D_CART },
	{ "L7", TC_MP_LTO7D_CART },
	{ "JC", TC_MP_JC },
	{ "JD", TC_MP_JD },
	{ "JK", TC_MP_JC },
	{ "JL", TC_MP_JD },
	{ "JY", TC_MP_JY },
	{ "JZ", TC_MP_JZ },
};

int cart_type_size = sizeof(cart_type) / sizeof(struct filedebug_tc_cart_type);

static int _filedebug_tc_write_schema(xmlTextWriterPtr writer, const struct filedebug_conf_tc *conf)
{
	int ret, i;

	/* Create XML document */
	ret = xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12254E", ret);
		return -1;
	}

	xmlTextWriterSetIndent(writer, 1);
	xmlTextWriterSetIndentString(writer, BAD_CAST "    ");

	xml_mktag(xmlTextWriterStartElement(writer, BAD_CAST "filedebug_cartridge_config"), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "dummy_io",
											  "%s", conf->dummy_io ? "true" : "false"), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "emulate_readonly",
											  "%s", conf->emulate_readonly ? "true" : "false"), -1);

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "capacity_mb",
											  "%"PRIu64, conf->capacity_mb), -1);

	for (i = 0; i < cart_type_size; i++) {
		if (conf->cart_type == cart_type[i].type_code) {
			xml_mktag(xmlTextWriterWriteFormatElement(writer,
													  BAD_CAST "cart_type",
													  "%s", cart_type[i].name), -1);
			break;
		}
	}

	xml_mktag(xmlTextWriterWriteFormatElement(writer,
											  BAD_CAST "density_code",
											  "%x", conf->density_code), -1);

	xml_mktag(xmlTextWriterEndElement(writer), -1);

	ret = xmlTextWriterEndDocument(writer);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12253E", ret);
		return -1;
	}

	return ret;
}

int filedebug_conf_tc_write_xml(char *filename, const struct filedebug_conf_tc *conf)
{
	int ret;
	xmlTextWriterPtr writer;

	/* Create XML writer. */
	writer = xmlNewTextWriterFilename(filename, 0);
	if (! writer) {
		ltfsmsg(LTFS_ERR, "12252E");
		return -1;
	}

	ret = _filedebug_tc_write_schema(writer, conf);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12251E");
	} else if (ret == 0) {
		ltfsmsg(LTFS_WARN, "12250W");
		ret = -1;
	}

	xmlFreeTextWriter(writer);

	return ret;
}

static int _filedebug_parser_init(xmlTextReaderPtr reader, const char *top_name)
{
	const char *name, *encoding;
	int type;

	if (xml_next_tag(reader, "", &name, &type) < 0)
		return -1;
	if (strcmp(name, top_name)) {
		ltfsmsg(LTFS_ERR, "12249E", name);
		return -1;
	}

	/* reject this XML file if it isn't UTF-8 */
	encoding = (const char *)xmlTextReaderConstEncoding(reader);
	if (! encoding || strcmp(encoding, "UTF-8")) {
		ltfsmsg(LTFS_ERR, "12248E", encoding);
		return -1;
	}

	return 0;
}

static int _filedebug_tc_parse_schema(xmlTextReaderPtr reader, struct filedebug_conf_tc *conf)
{
	int ret;
	unsigned long long value_ll;
	declare_parser_vars("filedebug_cartridge_config");

	i = 0; /* Dummy access for compiler warning */

	/* start the parser: find top-level "index" tag, check version and encoding */
	ret = _filedebug_parser_init(reader, parent_tag);
	if (ret < 0)
		return ret;

	/* parse index file contents */
	while (true) {
		get_next_tag();

		if (! strcmp(name, "dummy_io")) {
			get_tag_text();
			if (xml_parse_bool(&conf->dummy_io, value) < 0)
				return -1;
			check_tag_end("dummy_io");

		} else if (! strcmp(name, "emulate_readonly")) {
			get_tag_text();
			if (xml_parse_bool(&conf->emulate_readonly, value) < 0)
				return -1;
			check_tag_end("emulate_readonly");

		} else if (! strcmp(name, "capacity_mb")) {
			get_tag_text();

			if (xml_parse_ull(&value_ll, value) < 0)
				return -1;

			if (value_ll > 0)
				conf->capacity_mb = value_ll;
			else
				conf->capacity_mb = 3 * (GB / MB); /* 3GB by default */

			check_tag_end("capacity_mb");

		} else if (! strcmp(name, "cart_type")) {
			get_tag_text();

			for (i = 0; i < cart_type_size; i++) {
				conf->cart_type = TC_MP_LTO5D_CART;
				if (!strcmp(cart_type[i].name, value)) {
					conf->cart_type = cart_type[i].type_code;
					break;
				}
			}
			check_tag_end("cart_type");

		} else if (! strcmp(name, "density_code")) {
			get_tag_text();

			if (xml_parse_xll(&value_ll, value) < 0)
				return -1;
			conf->density_code = (char)value_ll;
			check_tag_end("density_code");

		}

	}

	return 0;
}

int filedebug_conf_tc_read_xml(char *filename, struct filedebug_conf_tc *conf)
{
	int ret;
	xmlTextReaderPtr reader;
	xmlDocPtr doc;

	reader = xmlReaderForFile(filename, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (! reader) {
		ltfsmsg(LTFS_ERR, "12246E");
		return -1;
	}

	/* Workaround for old libxml2 version on OS X 10.5: the method used to preserve
	 * unknown tags modifies the behavior of xmlFreeTextReader so that an additional
	 * xmlDocFree call is required to free all memory. */
	doc = xmlTextReaderCurrentDoc(reader);
	ret = _filedebug_tc_parse_schema(reader, conf);
	if (ret < 0) {
		ltfsmsg(LTFS_ERR, "12247E");
	}
	if (doc)
		xmlFreeDoc(doc);
	xmlFreeTextReader(reader);

	return ret;
}
