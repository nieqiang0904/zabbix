/*
** Zabbix
** Copyright (C) 2001-2014 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#ifdef ZBX_CUNIT
#include <malloc.h>

#define __USE_GNU
#include <dlfcn.h>

#include "common.h"
#include "zbxtypes.h"
#include "zbxgetopt.h"
#include "zbxcunit.h"

/* auto generated by make test */
#include "cu_suites.h"

#define ZBX_CU_ASSERT_ARGS_LENGTH	1024
#define ZBX_CU_ASSERT_NAME_LENGTH	128
#define ZBX_CU_ASSERT_BUFFER_SIZE	(ZBX_CU_ASSERT_ARGS_LENGTH * 2 + ZBX_CU_ASSERT_NAME_LENGTH + 16)

static char	zbx_cu_assert_args_buffer[ZBX_CU_ASSERT_BUFFER_SIZE];

const char	*zbx_cu_assert_args_str(const char *assert_name, const char *expression1, const char *actual,
		const char *expression2, const char *expected)
{
	size_t	offset = 0;

	offset = zbx_snprintf(zbx_cu_assert_args_buffer, ZBX_CU_ASSERT_NAME_LENGTH + 1, "%s(", assert_name);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 2, "%s=\"%s\", ",
			expression1, actual);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 1, "%s=\"%s\")",
			expression2, expected);

	return zbx_cu_assert_args_buffer;
}

const char	*zbx_cu_assert_args_str_n(const char *assert_name, const char *expression1, const char *actual,
		const char *expression2, const char *expected, size_t n)
{
	size_t	offset = 0;
	char	tmp[ZBX_CU_ASSERT_BUFFER_SIZE];

	zbx_strlcpy(tmp, actual, MIN(ZBX_CU_ASSERT_BUFFER_SIZE, n + 1));
	offset = zbx_snprintf(zbx_cu_assert_args_buffer, ZBX_CU_ASSERT_NAME_LENGTH + 1, "%s(", assert_name);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 2, "%s=\"%s\", ",
			expression1, tmp);

	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 1, "%s=\"%s\")",
			expression2, expected);

	return zbx_cu_assert_args_buffer;
}

const char	*zbx_cu_assert_args_ui64(const char *assert_name, const char *expression1, zbx_uint64_t actual,
		const char *expression2, zbx_uint64_t expected)
{
	size_t	offset = 0;

	offset = zbx_snprintf(zbx_cu_assert_args_buffer, ZBX_CU_ASSERT_NAME_LENGTH + 1, "%s(", assert_name);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 2, "%s=" ZBX_FS_UI64 ", ",
			expression1, actual);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 1, "%s=" ZBX_FS_UI64  ")",
			expression2, expected);

	return zbx_cu_assert_args_buffer;
}

const char	*zbx_cu_assert_args_dbl(const char *assert_name, const char *expression1, double actual,
		const char *expression2, double expected)
{
	size_t	offset = 0;

	offset = zbx_snprintf(zbx_cu_assert_args_buffer, ZBX_CU_ASSERT_NAME_LENGTH + 1, "%s(", assert_name);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 2, "%s=" ZBX_FS_DBL ", ",
			expression1, actual);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 1, "%s=" ZBX_FS_DBL  ")",
			expression2, expected);

	return zbx_cu_assert_args_buffer;
}

const char	*zbx_cu_assert_args_int(const char *assert_name, const char *expression1, int actual,
		const char *expression2, int expected)
{
	size_t	offset = 0;

	offset = zbx_snprintf(zbx_cu_assert_args_buffer, ZBX_CU_ASSERT_NAME_LENGTH + 1, "%s(", assert_name);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 2, "%s=%d, ",
			expression1, actual);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 1, "%s=%d)",
			expression2, expected);

	return zbx_cu_assert_args_buffer;
}

const char	*zbx_cu_assert_args_char(const char *assert_name, const char *expression1, char actual,
		const char *expression2, char expected)
{
	size_t	offset = 0;

	offset = zbx_snprintf(zbx_cu_assert_args_buffer, ZBX_CU_ASSERT_NAME_LENGTH + 1, "%s(", assert_name);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 2, "%s=%c (%d), ",
			expression1, actual, (int)actual);
	offset += zbx_snprintf(zbx_cu_assert_args_buffer + offset, ZBX_CU_ASSERT_ARGS_LENGTH + 1, "%s=%c (%d))",
			expression2, expected, (int)expected);

	return zbx_cu_assert_args_buffer;
}

/* long options */
static struct zbx_option	longopts[] =
{
	{"test",		2,	NULL,	't'},
	{"output",		2,	NULL,	'o'},
	{NULL}
};


/* short options */
static char	shortopts[] = "t::o::";

void	zbx_cu_run(int argc, char *argv[])
{
	const char			**suite;
	char				ch, *scope = NULL, *output = NULL, buffer[MAX_STRING_LEN];
	int				run_tests = 0, automated = 0;
	zbx_cu_init_suite_func_t	init_func;

	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL)))
	{
		switch (ch)
		{
			case 't':
				run_tests = 1;
				if (NULL != zbx_optarg)
					scope = zbx_strdup(NULL, zbx_optarg);
				break;
			case 'o':
				automated = 1;
				if (NULL != zbx_optarg)
					output = zbx_strdup(NULL, zbx_optarg);
		}
	}

	if (0 == run_tests)
	{
		zbx_free(output);
		return;
	}

	if (CUE_SUCCESS != CU_initialize_registry())
	{
		fprintf(stderr, "Error while initializing CUnit registry: %s\n", CU_get_error_msg());
		goto out;
	}

	for (suite = cu_suites; NULL != *suite; suite++)
	{
		if (NULL != scope && NULL == strstr(scope, *suite))
			continue;

		zbx_snprintf(buffer, sizeof(buffer), "%s%s", ZBX_CU_SUITE_PREFIX_STR, *suite);

		if (NULL != (init_func = dlsym(RTLD_DEFAULT, buffer)))
		{
			if (CUE_SUCCESS != init_func())
			{
				fprintf(stderr, "Error while initializing CUnit tests: %s\n", CU_get_error_msg());
				goto out;
			}
		}
	}

	if (0 == automated)
	{
		CU_basic_set_mode(CU_BRM_VERBOSE);
		CU_basic_run_tests();
	}
	else
	{
		if (NULL != output)
			CU_set_output_filename(output);

		CU_automated_run_tests();
		CU_list_tests_to_file();
	}

out:
	zbx_free(scope);
	zbx_free(output);

	exit(CU_get_error());
}

#endif

