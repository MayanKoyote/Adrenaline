/*
	Adrenaline VSH Control (VSHControl API)
	Copyright (C) 2016-2018, TheFloW
	Copyright (C) 2024-2025, isage
	Copyright (C) 2025, GrayJack

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

#ifndef __VSHCTRL_H__
#define __VSHCTRL_H__

#include <stddef.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <pspctrl.h>

/**
 * This api is for vsh menu. (flash0:/vsh/module/satelite.prx)
 *
 * The vsh menu is an user mode module, and because of this, these functions are
 * only available to user mode.
 */

/**
 * Registers the vsh menu.
 * When HOME is pressed, vshctrl will load the satelite module.
 * In module_start, call this function to register the vsh menu.
 *
 * @param ctrl - The function that will be executed each time
 * the system calls ReadBufferPositive. Despite satelite.prx being
 * an user module, this function will be executed in kernel mode.
 *
 * @returns 0 on success, < 0 on error.
 */
int vctrlVSHRegisterVshMenu(int (*ctrl)(SceCtrlData *, int));

/**
 * Exits the vsh menu.
 * vshmenu module must call this module after destroying vsh menu display and
 * freeing resources.
 *
 * vshmenu module doesn't need to stop-unload itself, as that is vshctrl job.
 *
 * @param conf - Indicates the new config. vshctrl will update the internal
 * vshctrl and systemctrl variables with the new configuration given by this
 * param. However is job of satelite.prx to save those settings to the
 * configuration file. using sctrlSESetConfig.
 *
 * @returns 0 on success, < 0 on error.
 */
int vctrlVSHExitVSHMenu(AdrenalineConfig *conf);

/**
 * Obtain the value of a registry variable.
 *
 * @param dir - registry directory.
 * @param name - variable name.
 * @param val - pointer to an unsigned 32 bit integer where the value will be
 * stored.
 */
int vctrlGetRegistryValue(const char *dir, const char *name, u32 *val);

/**
 * Set the value of a registry variable.
 *
 * @param dir - registry directory.
 * @param name - variable name.
 * @param val - new value of the variable.
 */
int vctrlSetRegistryValue(const char *dir, const char *name, u32 val);

#endif