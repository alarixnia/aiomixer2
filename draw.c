/* $NetBSD$ */
/*-
 * Copyright (c) 2021 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nia Alarie.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <curses.h>
#include <err.h>
#include <stdlib.h>
#include "draw.h"

static void bold_on(WINDOW *);
static void bold_off(WINDOW *);
static int get_enum_color(const char *);
static void draw_enum(struct aiomixer_control *, int, bool);
static void draw_set(struct aiomixer_control *, int);
static void draw_levels(struct aiomixer_control *,
    const struct mixer_level *, bool, bool);

static void
bold_on(WINDOW *w)
{
	/*
	 * Some (XXX: which?) legacy terminals do not support a Bold
	 * attribute.  In this case, we fall back to standout.
	 */
	if (termattrs() & A_BOLD)
		wattron(w, A_BOLD);
	else
		wattron(w, A_STANDOUT);
}

static void
bold_off(WINDOW *w)
{
	chtype attrs = getattrs(w);

	if (attrs & A_BOLD)
		wattroff(w, A_BOLD);
	if (attrs & A_STANDOUT)
		wattroff(w, A_STANDOUT);
}

void
draw_mixer_select(unsigned int num_mixers, unsigned int selected_mixer)
{
	struct audio_device dev;
	char mixer_path[16];
	unsigned int i;
	int fd;

	mvprintw(0, 0, "Select a mixer device:\n");

	for (i = 0; i < num_mixers; ++i) {
		(void)snprintf(mixer_path, sizeof(mixer_path),
		    "/dev/mixer%d", i);
		fd = open(mixer_path, O_RDWR);
		if (fd == -1)
			break;
		if (ioctl(fd, AUDIO_GETDEV, &dev) < 0) {
			close(fd);
			break;
		}
		close(fd);
		if (selected_mixer == i) {
			bold_on(stdscr);
			addstr("[*] ");
		} else {
			addstr("[ ] ");
		}
		printw("%s: %s %s %s\n", mixer_path,
		    dev.name, dev.version, dev.config);
		if (selected_mixer == i)
			bold_off(stdscr);
	}
}

void
draw_control(struct aiomixer *aio,
    struct aiomixer_control *control, bool selected)
{
	struct mixer_ctrl value;

	value.dev = control->info.index;
	value.type = control->info.type;
	if (value.type == AUDIO_MIXER_VALUE)
		value.un.value.num_channels = control->info.un.v.num_channels;

	if (ioctl(aio->fd, AUDIO_MIXER_READ, &value) < 0)
		err(EXIT_FAILURE, "failed to read from mixer device");

	wclear(control->widgetpad);
	if (selected) {
		bold_on(control->widgetpad);
		if (has_colors()) {
			wattron(control->widgetpad,
			    COLOR_PAIR(COLOR_CONTROL_SELECTED));
		}
		waddch(control->widgetpad, '*');
		if (has_colors()) {
			wattroff(control->widgetpad,
			    COLOR_PAIR(COLOR_CONTROL_SELECTED));
		}
	}
	wprintw(control->widgetpad, "%s\n", control->info.label.name);
	if (selected)
		bold_off(control->widgetpad);

	switch (value.type) {
	case AUDIO_MIXER_ENUM:
		draw_enum(control, value.un.ord, selected);
		break;
	case AUDIO_MIXER_SET:
		draw_set(control, value.un.mask);
		break;
	case AUDIO_MIXER_VALUE:
		draw_levels(control, &value.un.value,
		    aio->channels_unlocked, selected);
		break;
	}
}

void
draw_screen(struct aiomixer *aio)
{
	wnoutrefresh(stdscr);
	wnoutrefresh(aio->header);
	wnoutrefresh(aio->classbar);
	pnoutrefresh(aio->classes[aio->curclass].widgetpad,
	    aio->class_scroll_y, 0,
	    3, 0,
	    getmaxy(stdscr) - 3, getmaxx(stdscr));
	doupdate();
}

static int
get_enum_color(const char *name)
{
	if (strcmp(name, AudioNon) == 0) {
		return COLOR_ENUM_ON;
	}
	if (strcmp(name, AudioNoff) == 0) {
		return COLOR_ENUM_OFF;
	}

	return COLOR_ENUM_MISC;
}

static void
draw_enum(struct aiomixer_control *control, int ord, bool selected)
{
	struct audio_mixer_enum *e;
	int color = COLOR_ENUM_MISC;
	int i;

	for (i = 0; i < control->info.un.e.num_mem; ++i) {
		e = &control->info.un.e;
		if (ord == e->member[i].ord && selected)
			bold_on(control->widgetpad);
		waddch(control->widgetpad, '[');
		if (ord == e->member[i].ord) {
			if (has_colors()) {
				color = get_enum_color(e->member[i].label.name);
				wattron(control->widgetpad,
					COLOR_PAIR(color));
			} else {
				waddch(control->widgetpad, '*');
			}
		}
		wprintw(control->widgetpad, "%s", e->member[i].label.name);
		if (ord == control->info.un.e.member[i].ord) {
			if (has_colors()) {
				wattroff(control->widgetpad,
					COLOR_PAIR(color));
			}
		}
		waddch(control->widgetpad, ']');
		if (ord == e->member[i].ord && selected)
			bold_off(control->widgetpad);
		if (i != (e->num_mem - 1))
			waddstr(control->widgetpad, ", ");
	}
	waddch(control->widgetpad, '\n');
}

static void
draw_set(struct aiomixer_control *control, int mask)
{
	int i;

	for (i = 0; i < control->info.un.s.num_mem; ++i) {
		waddch(control->widgetpad, '[');
		if (mask & control->info.un.s.member[i].mask) {
			if (has_colors()) {
				wattron(control->widgetpad,
					COLOR_PAIR(COLOR_SET_SELECTED));
			}
			waddch(control->widgetpad, '*');
			if (has_colors()) {
				wattroff(control->widgetpad,
					COLOR_PAIR(COLOR_SET_SELECTED));
			}
		} else {
			waddch(control->widgetpad, ' ');
		}
		waddstr(control->widgetpad, "] ");
		if (control->setindex == i) {
			bold_on(control->widgetpad);
			waddch(control->widgetpad, '*');
		}
		wprintw(control->widgetpad, "%s",
		    control->info.un.s.member[i].label.name);
		if (control->setindex == i)
			bold_off(control->widgetpad);
		if (i != (control->info.un.s.num_mem - 1))
			waddstr(control->widgetpad, ", ");
	}
}

static void
draw_levels(struct aiomixer_control *control,
    const struct mixer_level *levels, bool channels_unlocked, bool selected)
{
	int i;
	int j, nchars;

	for (i = 0; i < control->info.un.v.num_channels; ++i) {
		if ((selected && !channels_unlocked) ||
		    (control->setindex == i && channels_unlocked)) {
			bold_on(control->widgetpad);
		}
		wprintw(control->widgetpad, "[%3u/%3u ",
		    levels->level[i], AUDIO_MAX_GAIN);
		if (has_colors()) {
			wattron(control->widgetpad,
				COLOR_PAIR(COLOR_LEVELS));
		}
		nchars = (levels->level[i] *
		    (getmaxx(control->widgetpad) - 11)) / AUDIO_MAX_GAIN;
		for (j = 0; j < nchars; ++j)
			waddch(control->widgetpad, '*');
		if (has_colors()) {
			wattroff(control->widgetpad,
				COLOR_PAIR(COLOR_LEVELS));
		}
		nchars = getmaxx(control->widgetpad) - 11 - nchars;
		for (j = 0; j < nchars; ++j)
			waddch(control->widgetpad, ' ');
		wprintw(control->widgetpad, "]\n");
		if ((selected && !channels_unlocked) ||
		    (control->setindex == i && channels_unlocked)) {
			bold_off(control->widgetpad);
		}
	}
}

void
draw_classbar(struct aiomixer *aio)
{
	unsigned int i;

	wmove(aio->classbar, 0, 0);

	for (i = 0; i < aio->numclasses; ++i) {
		if (aio->curclass == i)
			bold_on(aio->classbar);
		wprintw(aio->classbar, "[%u:", i + 1);
		if (aio->curclass == i) {
			if (has_colors()) {
				wattron(aio->classbar,
					COLOR_PAIR(COLOR_CONTROL_SELECTED));
			}
			waddch(aio->classbar, '*');
			if (has_colors()) {
				wattroff(aio->classbar,
					COLOR_PAIR(COLOR_CONTROL_SELECTED));
			}
		}
		waddstr(aio->classbar, aio->classes[i].name);
		if (aio->curclass == i)
			bold_off(aio->classbar);
		waddstr(aio->classbar, "] ");
	}

	wprintw(aio->classbar, "\n\n");
}

void
draw_header(struct aiomixer *aio)
{
	mvwaddstr(aio->header, 0,
	    getmaxx(aio->header) - (int)sizeof("NetBSD audio mixer") + 1,
	    "NetBSD audio mixer");

	if (aio->mixerdev.version[0] != '\0') {
		wprintw(aio->header, "%s %s",
		    aio->mixerdev.name, aio->mixerdev.version);
	} else {
		wprintw(aio->header, "%s", aio->mixerdev.name);
	}
}

void
create_widgets(struct aiomixer *aio)
{
	size_t i, j;
	struct aiomixer_class *class;
	struct aiomixer_control *control;

	aio->header = newwin(1, getmaxx(stdscr), 0, 0);
	if (aio->header == NULL)
		errx(EXIT_FAILURE, "failed to create window");

	aio->classbar = newwin(2, getmaxx(stdscr), 1, 0);
	if (aio->classbar == NULL)
		errx(EXIT_FAILURE, "failed to create window");

	for (i = 0; i < aio->numclasses; ++i) {
		class = &aio->classes[i];
		class->height = 0;
		class->widgetpad = newpad(4 * __arraycount(class->controls),
		    getmaxx(stdscr));
		if (class->widgetpad == NULL)
			errx(EXIT_FAILURE, "failed to create curses pad");
		for (j = 0; j < class->numcontrols; ++j) {
			control = &class->controls[j];
			switch (control->info.type) {
			case AUDIO_MIXER_VALUE:
				control->height = 2 + control->info.un.v.num_channels;
				break;
			case AUDIO_MIXER_ENUM:
			case AUDIO_MIXER_SET:
				control->height = 3;
				break;
			}
			control->widgetpad = subpad(class->widgetpad,
			    control->height, getmaxx(stdscr),
			    class->height, 0);
			if (control->widgetpad == NULL)
				errx(EXIT_FAILURE, "failed to create curses pad");
			control->widget_y = class->height;
			class->height += control->height;
		}
	}
}

void
resize_widgets(struct aiomixer *aio)
{
	size_t i, j;
	struct aiomixer_class *class;
	struct aiomixer_control *control;

	wresize(aio->header, 1, getmaxx(stdscr));
	wresize(aio->classbar, 2, getmaxx(stdscr));

	for (i = 0; i < aio->numclasses; ++i) {
		class = &aio->classes[i];
		wresize(class->widgetpad,
		    4 * __arraycount(class->controls), getmaxx(stdscr));
		for (j = 0; j < class->numcontrols; ++j) {
			control = &class->controls[j];
			wresize(control->widgetpad,
			    control->height, getmaxx(stdscr));
		}
	}
}
