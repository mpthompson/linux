/*
 * Copyright (C) 2014 Michael P. Thompson, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
 
#include <linux/err.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include "clk.h"

DEFINE_SPINLOCK(n329_lock);
