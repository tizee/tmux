/* $OpenBSD$ */

/*
 * Copyright (c) 2025 tizee <tizee@github.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include "tmux.h"

/*
 * Enter capture mode: ensure the pane is in copy mode, then arm the capture
 * hint overlay. Bindable from the root table so a single key enters capture
 * mode from a normal pane; if the pane is already in copy mode the overlay is
 * armed directly without losing the current scroll position.
 */

static enum cmd_retval	cmd_capture_mode_exec(struct cmd *, struct cmdq_item *);

const struct cmd_entry cmd_capture_mode_entry = {
	.name = "capture-mode",
	.alias = NULL,

	.args = { "t:", 0, 0, NULL },
	.usage = CMD_TARGET_PANE_USAGE,

	.target = { 't', CMD_FIND_PANE, 0 },

	.flags = CMD_AFTERHOOK,
	.exec = cmd_capture_mode_exec
};

static enum cmd_retval
cmd_capture_mode_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args			*args = cmd_get_args(self);
	struct cmd_find_state		*target = cmdq_get_target(item);
	struct client			*c = cmdq_get_client(item);
	struct window_pane		*wp = target->wp;
	struct window_mode_entry	*wme;

	/*
	 * Capture mode is an interactive overlay: it draws hint labels and posts
	 * status messages on a client. Without an attached client session there is
	 * nothing to drive, and the status/overlay paths dereference c->session.
	 * Bail out as a no-op rather than crash the server (e.g. when invoked as a
	 * plain command with no attached client).
	 */
	if (c == NULL || c->session == NULL)
		return (CMD_RETURN_NORMAL);

	/* Enter copy mode first if the pane is not already in it. */
	wme = TAILQ_FIRST(&wp->modes);
	if (wme == NULL || wme->mode != &window_copy_mode) {
		window_pane_set_mode(wp, wp, &window_copy_mode, NULL, args);
		window_copy_set_line_numbers(wp, 1);
	}

	window_copy_capture_pane(wp, c);

	return (CMD_RETURN_NORMAL);
}
