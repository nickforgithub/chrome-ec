/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_RBOX, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_RBOX, format, ## args)

static int command_wp(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;

		/* Invert, because active low */
		GREG32(RBOX, EC_WP_L) = !val;
	}

	/* Invert, because active low */
	val = !GREG32(RBOX, EC_WP_L);

	ccprintf("Flash WP is %s\n", val ? "enabled" : "disabled");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(wp, command_wp,
			"[<BOOLEAN>]",
			"Get/set the flash HW write-protect signal");

/* When the system is locked down, provide a means to unlock it */
#ifdef CONFIG_RESTRICTED_CONSOLE_COMMANDS

/* TODO(crosbug.com/p/55510): It should be locked by default */
static int console_restricted_state;

int console_is_restricted(void)
{
	return console_restricted_state;
}

/****************************************************************************/
/* Stuff for the unlock dance */

/* Total time to spend poking the power button */
#define DANCE_TIME (10 * SECOND)
/* Max time between pokes */
#define DANCE_BEAT (2 * SECOND)

static timestamp_t dance_deadline;
static int dance_in_progress;

/* This will only be invoked when the dance is done, either good or bad. */
static void dance_is_over(void)
{
	if (dance_in_progress) {
		CPRINTS("Unlock dance failed");
	} else {
		CPRINTS("Unlock dance completed successfully");
		console_restricted_state = 0;
	}

	dance_in_progress = 0;

	/* Disable power button interrupt */
	GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 0);
	task_disable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);

	/* Allow sleeping again */
	enable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
}
DECLARE_DEFERRED(dance_is_over);

static void power_button_poked(void)
{
	if (timestamp_expired(dance_deadline, NULL)) {
		/* We've been poking for long enough */
		dance_in_progress = 0;
		hook_call_deferred(&dance_is_over_data, 0);
		CPRINTS("poke: enough already", __func__);
	} else {
		/* Wait for the next poke */
		hook_call_deferred(&dance_is_over_data, DANCE_BEAT);
		CPRINTS("poke");
	}

	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);
}
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT, power_button_poked, 1);


static int start_the_dance(void)
{
	/* Don't invoke more than one at a time */
	if (dance_in_progress)
		return EC_ERROR_BUSY;

	dance_in_progress = 1;

	/* Clear any leftover power button interrupts */
	GWRITE_FIELD(RBOX, INT_STATE, INTR_PWRB_IN_FED, 1);

	/* Enable power button interrupt */
	GWRITE_FIELD(RBOX, INT_ENABLE, INTR_PWRB_IN_FED, 1);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_PWRB_IN_FED_INT);

	/* Keep dancing until it's been long enough */
	dance_deadline = get_time();
	dance_deadline.val += DANCE_TIME;

	/* Stay awake while we're doing this, just in case. */
	disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);

	/* Check progress after waiting long enough for one button press */
	hook_call_deferred(&dance_is_over_data, DANCE_BEAT);

	CPRINTS("Unlock dance starting. Dance until %.6ld", dance_deadline);

	return EC_SUCCESS;
}

/****************************************************************************/

static int command_lock(int argc, char **argv)
{
	int enabled;
	int i;

	if (argc > 1) {
		if (!parse_bool(argv[1], &enabled))
			return EC_ERROR_PARAM1;

		/* Changing nothing does nothing */
		if (enabled == console_restricted_state)
			goto out;

		/* Locking the console is always allowed */
		if (enabled)  {
			console_restricted_state = 1;
			goto out;
		}

		/*
		 * TODO(crosbug.com/p/55322, crosbug.com/p/55728): There may be
		 * other preconditions which must be satisified before
		 * continuing. We can return EC_ERROR_ACCESS_DENIED if those
		 * aren't met.
		 */

		/* Don't count down if we know it's likely to fail */
		if (dance_in_progress) {
			ccprintf("An unlock dance is already in progress\n");
			return EC_ERROR_BUSY;
		}

		/* Now the user has to sit there and poke the button */
		ccprintf("Start poking the power button in ");
		for (i = 5; i; i--) {
			ccprintf("%d ", i);
			sleep(1);
		}
		ccprintf("go!\n");

		return start_the_dance();
	}

out:
	ccprintf("The restricted console lock is %s\n",
		 console_is_restricted() ? "enabled" : "disabled");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(lock, command_lock,
			     "[<BOOLEAN>]",
			     "Get/Set the restricted console lock");

#endif	/* CONFIG_RESTRICTED_CONSOLE_COMMANDS */
