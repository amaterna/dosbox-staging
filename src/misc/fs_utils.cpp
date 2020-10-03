/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2019-2020  The dosbox-staging team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef DOSBOX_FS_UTILS_H
#define DOSBOX_FS_UTILS_H

#include "fs_utils.h"

#include <cassert>
#include <glob.h>
#include <string>
#include <sys/stat.h>

#include "compiler.h"
#include "logging.h"

bool fs_exists(const char *path) noexcept
{
	struct stat sb;
	return (stat(path, &sb) == 0);
}

static std::string translate_to_glob_pattern(const std::string &win_path)
{
	std::string glob_pattern;
	glob_pattern.reserve(win_path.size() * 4);
	char one_char_pattern[5] = "[Aa]";
	for (char c : win_path) {
		if (isalpha(c)) {
			one_char_pattern[1] = tolower(c);
			one_char_pattern[2] = toupper(c);
			glob_pattern.append(one_char_pattern);
			continue;
		}
		switch (c) {
		case '\\':
			glob_pattern.push_back('/');
			continue;
		case '?':
		case '*':
		case '[':
		case ']':
			glob_pattern.push_back('\\');
			glob_pattern.push_back(c);
			continue;
		default:
			glob_pattern.push_back(c);
			continue;
		}
	}
	return glob_pattern;
}

std::string to_posix_path(const std::string &win_path)
{
	// TODO if exact path (except separator) exists, then just return it
	const std::string pattern = translate_to_glob_pattern(win_path);
	glob_t pglob;
	const int err = glob(pattern.c_str(), GLOB_TILDE_CHECK, nullptr, &pglob);
	if (err == GLOB_NOMATCH) {
		DEBUG_LOG_MSG(":: NOMATCH");
		globfree(&pglob);
		return "";
	}
	if (err != 0) {
		DEBUG_LOG_MSG(":: other error");
		globfree(&pglob);
		return "";
	}
	if (pglob.gl_pathc > 1) {
		DEBUG_LOG_MSG("Warning: searching for path '%s' gives ambiguous results:",
		              win_path.c_str());
		for (size_t i = 0; i < pglob.gl_pathc; i++) {
			DEBUG_LOG_MSG("'%s'", pglob.gl_pathv[i]);
		}
	}
	std::string ret = pglob.gl_pathv[0];
	globfree(&pglob);
	return ret;
}

#endif
