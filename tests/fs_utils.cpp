/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2020-2020  The dosbox-staging team
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

#include "fs_utils.h"

#include <gtest/gtest.h>

#include <string>

namespace {

TEST(PathExists, DirExists)
{
	EXPECT_TRUE(fs_exists("tests"));
}

TEST(PathExists, FileExists)
{
	EXPECT_TRUE(fs_exists("tests/fs_utils.cpp"));
}

TEST(PathExists, MissingPath)
{
	EXPECT_FALSE(fs_exists("foobar"));
}

TEST(PathConversion, SimpleTest)
{
	constexpr auto expected_result = "tests/files/paths/empty.txt";
	constexpr auto input = "tests\\files\\PATHS\\EMPTY.TXT";
	ASSERT_TRUE(fs_exists(expected_result));
	EXPECT_EQ(expected_result, to_posix_path(input));
}

} // namespace
