/*
	Adrenaline
	Copyright (C) 2016-2018, TheFloW

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __OPTIONS_H__
#define __OPTIONS_H__

static char *disenabled[] = { "Disabled", "Enabled" };
static char *endisabled[] = { "Enabled", "Disabled" };

static char *umdmodes[] = { "Inferno", "M33 Driver", "Sony NP9660" };
static char *extendedcolors[] = { "Disabled", "02g", "03g" };
static char *cpuspeeds[] = { "Default", "20/10", "75/37", "100/50", "133/66", "222/111", "266/133", "300/150", "333/166" };
static char *highmem[] = { "Default", "Stable", "Max" };

static char *buttonassign[] = { "O is enter", "X is enter" };

static char *regions[] = {
	"Disabled",
	"Japan",
	"America",
	"Europe",
	"Korea",
	"United Kingdom",
	"Mexico",
	"Australia/NZ",
	"East",
	"Taiwan",
	"Russia",
	"China",
	"Debug Type I",
	"Debug Type II"
};

static char *iso_cache[] = {"LRU", "RR", "Off"};
static char *iso_cache_num[] = {"Auto", "1", "2", "4", "8", "16", "32", "64", "128"};
static char *iso_cache_size[] = {"Auto", "1KB", "2KB", "4KB", "8KB", "16KB", "32KB", "64KB"};
static char *umd_seek_read_delay[] = {"Off", "1x", "2x", "3x", "4x"};

#endif