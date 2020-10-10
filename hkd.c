/*
 * Copyright (c) 2020 Alessandro Mauri
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <wordexp.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdarg.h>
#include "keys.h"

/* Value defines */
#define FILE_NAME_MAX_LENGTH 255
#define KEY_BUFFER_SIZE 16
#define BLOCK_SIZE 512

/* ANSI colors escape codes */
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/* Macro functions */
#define yellow(str) (ANSI_COLOR_YELLOW str ANSI_COLOR_RESET)
#define green(str) (ANSI_COLOR_GREEN str ANSI_COLOR_RESET)
#define red(str) (ANSI_COLOR_RED str ANSI_COLOR_RESET)
#define test_bit(yalv, abs_b) ((((char *)abs_b)[yalv/8] & (1<<yalv%8)) > 0)
#define array_size(val) (val ? sizeof(val)/sizeof(val[0]) : 0)
#define array_size_const(val) ((int)(sizeof(val)/sizeof(val[0])))

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024*(EVENT_SIZE+16))
#define EVDEV_ROOT_DIR "/dev/input/"
#define LOCK_FILE "/tmp/hkd.lock"

const char *config_paths[] = {
	"$XDG_CONFIG_HOME/hkd/config",
	"$HOME/.config/hkd/config",
	"/etc/hkd/config",
};

struct key_buffer {
	unsigned short buf[KEY_BUFFER_SIZE];
	unsigned int size;
};

/* Hotkey list: linked list that holds all valid hoteys parsed from the
 * config file and the corresponding command */
struct hotkey_list_e {
	struct key_buffer kb;
	char *command;
	int fuzzy;
	struct hotkey_list_e *next;
};

struct hotkey_list_e *hotkey_list = NULL;
unsigned long hotkey_size_mask = 0;
char *ext_config_file = NULL;
/* Global flags */
int vflag = 0;
int dead = 0; /* Exit flag */
/* key buffer operations */
int key_buffer_add (struct key_buffer*, unsigned short);
int key_buffer_remove (struct key_buffer*, unsigned short);
int key_buffer_compare_fuzzy (struct key_buffer *, struct key_buffer *);
int key_buffer_compare (struct key_buffer *, struct key_buffer *);
void key_buffer_reset (struct key_buffer *);
/* Other operations */
void int_handler (int signum);
void exec_command (char *);
void parse_config_file (void);
void update_descriptors_list (int **, int *);
void remove_lock (void);
void die (const char *, ...);
void usage (void);
int prepare_epoll (int *, int, int);
unsigned short key_to_code (char *);
const char * code_to_name (unsigned int);
/* hotkey list operations */
void hotkey_list_add (struct hotkey_list_e *, struct key_buffer *, char *, int);
void hotkey_list_destroy (struct hotkey_list_e *);

int main (int argc, char *argv[])
{
	int fd_num = 0;
	int *fds = NULL;
	int lock_file_descriptor;
	int opc;
	int ev_fd;
	int event_watcher = inotify_init1(IN_NONBLOCK);
	int dump = 0;
	ssize_t read_b; 				/* Read buffer */
	struct flock fl;
	struct sigaction action;
	struct input_event event;
	struct key_buffer pb = {{0}, 0};	/* Pressed keys buffer */

	/* Parse command line arguments */
	while ((opc = getopt(argc, argv, "vc:dh")) != -1) {
		switch (opc) {
		case 'v':
			vflag = 1;
			break;
		case 'c':
			ext_config_file = malloc(strlen(optarg) + 1);
			if (!ext_config_file)
				die("malloc in main():");
			 strcpy(ext_config_file, optarg);
			 break;
		case 'd':
			dump = 1;
			break;
		case 'h':
			usage();
			break;
		break;
		}
	}

	/* Handle SIGINT */
	dead = 0;
	memset(&action, 0, sizeof(action));
	action.sa_handler = int_handler;
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGUSR1, &action, NULL);
	sigaction(SIGCHLD, &action, NULL);

	/* Parse config file */
	parse_config_file();

	/* Check if hkd is already running */
	lock_file_descriptor = open(LOCK_FILE, O_RDWR | O_CREAT, 0600);
	if (lock_file_descriptor < 0)
		die("Can't open lock file:");
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	if (fcntl(lock_file_descriptor, F_SETLK, &fl) < 0)
		die("hkd is already running");
	atexit(remove_lock);

	/* If a dump is requested print the hotkey list then exit */
	if (dump) {
		printf("DUMPING HOTKEY LIST\n\n");
		for (struct hotkey_list_e *tmp = hotkey_list; tmp; tmp = tmp->next) {
			printf("Hotkey\n");
			printf("\tKeys: ");
			for (unsigned int i = 0; i < tmp->kb.size; i++)
				printf("%s ", code_to_name(tmp->kb.buf[i]));
			printf("\n\tMatching: %s\n", tmp->fuzzy ? "fuzzy" : "ordered");
			printf("\tCommand: %s\n\n", tmp->command);
		}
		exit(EXIT_SUCCESS);
	}

	/* Load descriptors */
	update_descriptors_list(&fds, &fd_num);

	/* Prepare directory update watcher */
	if (event_watcher < 0)
		die("Could not call inotify_init:");
	if (inotify_add_watch(event_watcher, EVDEV_ROOT_DIR, IN_CREATE | IN_DELETE) < 0)
		die("Could not add /dev/input to the watch list:");

	/* Prepare epoll list */
	ev_fd = prepare_epoll(fds, fd_num, event_watcher);

	/* MAIN EVENT LOOP */
	mainloop_begin:
	for (;;) {
		int t = 0;
		static unsigned int prev_size;
		static struct epoll_event ev_type;
		struct hotkey_list_e *tmp;
		char buf[EVENT_BUF_LEN];

		/* On linux use epoll(2) as it gives better performance */
		if (epoll_wait(ev_fd, &ev_type, fd_num, -1) < 0 || dead) {
			if (errno != EINTR)
				break;
		}

		if (ev_type.events != EPOLLIN)
			continue;

		if (read(event_watcher, buf, EVENT_BUF_LEN) >= 0) {
			sleep(1); // wait for devices to settle
			update_descriptors_list(&fds, &fd_num);
			if (close(ev_fd) < 0)
				die("Could not close event filedescriptors list (ev_fd):");
			ev_fd = prepare_epoll(fds, fd_num, event_watcher);
			goto mainloop_begin;
		}

		prev_size = pb.size;
		for (int i = 0; i < fd_num; i++) {

			read_b = read(fds[i], &event, sizeof(struct input_event));
			if (read_b != sizeof(struct input_event)) continue;

			/* Ignore touchpad events */
			if (
				event.type == EV_KEY &&
				event.code != BTN_TOUCH &&
				event.code != BTN_TOOL_FINGER &&
				event.code != BTN_TOOL_DOUBLETAP &&
				event.code != BTN_TOOL_TRIPLETAP
				) {
				switch (event.value) {
				/* Key released */
				case 0:
					key_buffer_remove(&pb, event.code);
					break;
				/* Key pressed */
				case 1:
					key_buffer_add(&pb, event.code);
					break;
				}
			}
		}

		if (pb.size <= prev_size)
			continue;

		if (vflag) {
			printf("Pressed keys: ");
			for (unsigned int i = 0; i < pb.size; i++)
				printf("%s ", code_to_name(pb.buf[i]));
			putchar('\n');
		}

		if (hotkey_size_mask & 1 << (pb.size - 1)) {
			for (tmp = hotkey_list; tmp != NULL; tmp = tmp->next) {
				if (tmp->fuzzy)
					t = key_buffer_compare_fuzzy(&pb, &tmp->kb);
				else
					t = key_buffer_compare(&pb, &tmp->kb);
				if (t)
					exec_command(tmp->command);

			}
		}
	}

	// TODO: better child handling, for now all children receive the same
	// interrupts as the father so everything should work fine
	wait(NULL);
	if (!dead)
		fprintf(stderr, red("An error occured: %s\n"), errno ? strerror(errno): "idk");
	close(ev_fd);
	close(event_watcher);
	for (int i = 0; i < fd_num; i++)
		if (close(fds[i]) == -1)
			die("Error closing file descriptors:");
	return 0;
}

/* Adds a keycode to the pressed buffer if it is not already present
 * Returns non zero if the key was not added. */
int key_buffer_add (struct key_buffer *pb, unsigned short key)
{
	if (!pb) return 1;
	/* Linear search if the key is already buffered */
	for (unsigned int i = 0; i < pb->size; i++)
		if (key == pb->buf[i]) return 1;

	if (pb->size >= KEY_BUFFER_SIZE)
		return 1;

	pb->buf[pb->size++] = key;

	return 0;
}

/* Removes a keycode from a pressed buffer if it is present returns
 * non zero in case of failure (key not present or buffer empty). */
int key_buffer_remove (struct key_buffer *pb, unsigned short key)
{
	if (!pb) return 1;

	for (unsigned int i = 0; i < pb->size; i++) {
		if (pb->buf[i] == key) {
			pb->size--;
			pb->buf[i] = pb->buf[pb->size];
			return 0;
		}
	}
	return 1;
}

void key_buffer_reset (struct key_buffer *kb)
{
	kb->size = 0;
	memset(kb->buf, 0, KEY_BUFFER_SIZE * sizeof(unsigned short));
}

void int_handler (int signum)
{
	switch (signum) {
	case SIGINT:
		if (dead)
			die("An error occured, exiting");
		if (vflag)
			printf(yellow("Received interrupt signal, exiting gracefully...\n"));
		dead = 1;
		break;
	case SIGUSR1:
		parse_config_file();
		break;
	case SIGCHLD:
		wait(NULL);
		break;
	}
}

/* Executes a command from a string */
void exec_command (char *command)
{
	static wordexp_t result;

	/* Expand the string for the program to run */
	switch (wordexp (command, &result, 0)) {
	case 0:
		break;
	case WRDE_NOSPACE:
		/* If the error was WRDE_NOSPACE,
		 * then perhaps part of the result was allocated */
		wordfree (&result);
		return;
	default:
		/* Some other error */
		fprintf(stderr, "Could not parse, %s is not valid\n", command);
		return;
	}

	pid_t cpid;
	switch (cpid = fork()) {
	case -1:
		fprintf(stderr, "Could not create child process: %s", strerror(errno));
		wordfree(&result);
		break;
	case 0:
		/* This is the child process, execute the command */
		execvp(result.we_wordv[0], result.we_wordv);
		die("%s:", command);
		break;
	default:
		while (waitpid(cpid, NULL, WNOHANG) == -1) {}
		wordfree(&result);
		break;
	}
}

void update_descriptors_list (int **fds, int *fd_num)
{
	struct dirent *file_ent;
	char ev_path[sizeof(EVDEV_ROOT_DIR) + FILE_NAME_MAX_LENGTH + 1];
	void *tmp_p;
	int tmp_fd;
	unsigned char evtype_b[EV_MAX];
	/* Open the event directory */
	DIR *ev_dir = opendir(EVDEV_ROOT_DIR);
	if (!ev_dir)
		die("Could not open /dev/input:");

	(*fd_num) = 0;

	for (;;) {

		if ((file_ent = readdir(ev_dir)) == NULL)
			break;
		/* Filter out non character devices */
		if (file_ent->d_type != DT_CHR)
			continue;

		/* Compose absolute path from relative */
		strncpy(ev_path, EVDEV_ROOT_DIR, sizeof(EVDEV_ROOT_DIR) + FILE_NAME_MAX_LENGTH);
	   	strncat(ev_path, file_ent->d_name, sizeof(EVDEV_ROOT_DIR) + FILE_NAME_MAX_LENGTH);

		/* Open device and check if it can give key events otherwise ignore it */
		tmp_fd = open(ev_path, O_RDONLY | O_NONBLOCK);
		if (tmp_fd < 0) {
			if (vflag)
				printf(red("Could not open device %s\n"), ev_path);
			continue;
		}

		memset(evtype_b, 0, sizeof(evtype_b));
		if (ioctl(tmp_fd, EVIOCGBIT(0, EV_MAX), evtype_b) < 0) {
			if (vflag)
				printf(red("Could not read capabilities of device %s\n"),ev_path);
			close(tmp_fd);
			continue;
		}

		if (!test_bit(EV_KEY, evtype_b)) {
			if (vflag)
				printf(yellow("Ignoring device %s\n"), ev_path);
			close(tmp_fd);
			continue;
		}

		tmp_p = realloc((*fds), sizeof(int) * ((*fd_num) + 1));
		if (!tmp_p)
			die("realloc file descriptors:");
		(*fds) = (int *) tmp_p;

		(*fds)[(*fd_num)] = tmp_fd;
		(*fd_num)++;
	}
	closedir(ev_dir);
	if (*fd_num) {
		if (vflag)
			printf(green("Monitoring %d devices\n"), *fd_num);
	} else {
		die("Could not open any devices, exiting");
	}
}

int prepare_epoll (int *fds, int fd_num, int event_watcher)
{
 	int ev_fd = epoll_create(1);
	static struct epoll_event epoll_read_ev;
 	epoll_read_ev.events = EPOLLIN;
 	if (ev_fd < 0)
 		die("epoll_create failed in prepare_epoll:");
 	if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, event_watcher, &epoll_read_ev) < 0)
 		die("Could not add file descriptor to the epoll list:");
 	for (int i = 0; i < fd_num; i++)
 		if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, fds[i], &epoll_read_ev) < 0)
 			die("Could not add file descriptor to the epoll list:");
	return ev_fd;
}

/* Checks if two key buffers contain the same keys in no specified order */
int key_buffer_compare_fuzzy (struct key_buffer *haystack, struct key_buffer *needle)
{
	int ff = 0;
	if (haystack->size != needle->size)
		return 0;
	for (int x = needle->size - 1; x >= 0; x--) {
		for (unsigned int i = 0; i < haystack->size; i++)
			ff += (needle->buf[x] == haystack->buf[i]);
		if (!ff)
			return 0;
		ff = 0;
	}
	return 1;
}

/* Checks if two key buffers are the same (same order) */
int key_buffer_compare (struct key_buffer *haystack, struct key_buffer *needle)
{
	if (haystack->size != needle->size)
		return 0;
	for (unsigned int i = 0; i < needle->size; i++) {
		if (needle->buf[i] != haystack->buf[i])
			return 0;
	}
	return 1;
}

void hotkey_list_destroy (struct hotkey_list_e *head)
{
	struct hotkey_list_e *tmp;
	for (; head; free(tmp)) {
		if (head->command)
			free(head->command);
		tmp = head;
		head = head->next;
	}
}

void hotkey_list_add (struct hotkey_list_e *head, struct key_buffer *kb, char *cmd, int f)
{
	int size;
	struct hotkey_list_e *tmp;
	if (!(size = strlen(cmd)))
		return;
	if (!(tmp = malloc(sizeof(struct hotkey_list_e))))
		die("Memory allocation failed in hotkey_list_add():");
	if (!(tmp->command = malloc(size + 1)))
		die("Memory allocation failed in hotkey_list_add():");
	strcpy(tmp->command, cmd);
	tmp->kb = *kb;
	tmp->fuzzy = f;
	tmp->next = NULL;

	if (head) {
		for (; head->next; head = head->next);
		head->next = tmp;
	} else
		hotkey_list = tmp;
}

void parse_config_file (void)
{
	wordexp_t result = {0};
	FILE *fd;
	/* normal, skip line, get matching, get keys, get command, output */
	enum {NORM, LINE_SKIP, GET_MATCH, GET_KEYS, GET_CMD, LAST} parse_state = NORM;
	enum {CONT, NEW_BL, LAST_BL, END} block_state = CONT; /* continue, new block, last block, end */
	int alloc_tmp = 0, alloc_size = 0;
	int fuzzy = 0;
	int i_tmp = 0, linenum = 1;
	char block[BLOCK_SIZE + 1] = {0};
	char *bb = NULL;
	char *keys = NULL;
	char *cmd = NULL;
	char *cp_tmp = NULL;
	struct key_buffer kb;
	unsigned short us_tmp = 0;

	key_buffer_reset(&kb);
	if (ext_config_file) {
		switch (wordexp(ext_config_file, &result, 0)) {
		case 0:
			break;
		case WRDE_NOSPACE:
			/* If the error was WRDE_NOSPACE,
		 	 * then perhaps part of the result was allocated */
			wordfree (&result);
			die("Not enough space:");
		default:
			die("Path not valid:");
		}

		fd = fopen(result.we_wordv[0], "r");
		wordfree(&result);
		if (!fd)
			die("Error opening config file:");
		free(ext_config_file);
		ext_config_file = NULL;
	} else {
		for (int i = 0; i < array_size_const(config_paths); i++) {
			switch (wordexp(config_paths[i], &result, 0)) {
			case 0:
				break;
			case WRDE_NOSPACE:
				/* If the error was WRDE_NOSPACE,
		 		 * then perhaps part of the result was allocated */
				wordfree (&result);
				die("Not enough space:");
			default:
				die("Path not valid:");
			}

			fd = fopen(result.we_wordv[0], "r");
			wordfree(&result);
			if (fd)
				break;
			if (vflag)
				printf(yellow("config file not found at %s\n"), config_paths[i]);
		}
		if (!fd)
			die("Could not open any config files, check the log for more details");
	}

	hotkey_list_destroy(hotkey_list);
	hotkey_list = NULL;
	while (block_state != END) {
		int tmp = 0;
		memset(block, 0, BLOCK_SIZE + 1);
		tmp = fread(block, sizeof(char), BLOCK_SIZE, fd);
		if (!tmp)
			break;
		if (tmp < BLOCK_SIZE || feof(fd))
			block_state = LAST_BL;
		else
			block_state = CONT;
		bb = block;

		while (block_state == CONT || block_state == LAST_BL) {
			switch (parse_state) {
			// First state
			case NORM:
				// remove whitespaces
				while (isblank(*bb) && *bb)
					bb++;
				// get state
				switch (*bb) {
#if defined(__X86_64__) || defined(__i386__)
				case EOF:
#endif
				case '\0':
					// If it is the end of the last block exit
					block_state = block_state == LAST_BL ? END : NEW_BL;
					break;
				case '\n':
				case '#':
					parse_state = LINE_SKIP;
					break;
				default:
					parse_state = GET_MATCH;
					break;
				}
				break;
			// Skip line (comment)
			case 1:
				while (*bb != '\n' && *bb)
					bb++;
				if (*bb) {
					bb++;
					linenum++;
					parse_state = NORM;
				} else {
					block_state = NEW_BL;
				}
				break;
			// Get compairson method
			case 2:
				switch (*bb) {
				case '-':
					fuzzy = 0;
					break;
				case '*':
					fuzzy = 1;
					break;
				default:
					die("Error at line %d: "
					"hotkey definition must start with '-' or '*'",
					linenum);
					break;
				}
				bb++;
				parse_state = GET_KEYS;
				break;
			// Get keys
			case 3:
				if (!keys) {
					if (!(keys = malloc(alloc_size = (sizeof(char) * 64))))
						die("malloc for keys in parse_config_file():");
					memset(keys, 0, alloc_size);
				} else if (alloc_tmp >= alloc_size) {
					if (!(keys = realloc(keys, alloc_size = alloc_size * 2)))
						die("realloc for keys in parse_config_file():");
					memset(&keys[alloc_size / 2], 0, alloc_size / 2);
				}

				for (alloc_tmp = 0; bb[alloc_tmp] &&
				bb[alloc_tmp] != ':' && bb[alloc_tmp] != '\n' &&
				alloc_tmp < alloc_size; alloc_tmp++);

				if (!bb[alloc_tmp] || alloc_tmp == alloc_size) {
					strncat(keys, bb, alloc_tmp);
					bb += alloc_tmp;
					if (block_state == LAST_BL)
						die("Keys not finished before end of file");
					else
						block_state = NEW_BL;
					break;
				} else if (bb[alloc_tmp] == ':') {
					strncat(keys, bb, alloc_tmp);
					bb += alloc_tmp + 1;
					parse_state = GET_CMD;
					break;
				} else {
					die("Error at line %d: "
					"no command specified, missing ':' after keys",
					linenum);
				}
				break;
			// Get command
			case 4:
				if (!cmd) {
					if (!(cmd = malloc(alloc_size = (sizeof(char) * 128))))
						die("malloc for cmd in parse_config_file():");
					memset(cmd, 0, alloc_size);
				} else if (alloc_tmp >= alloc_size) {
					if (!(cmd = realloc(cmd, alloc_size = alloc_size * 2)))
						die("realloc for cmd in parse_config_file():");
					memset(&cmd[alloc_size / 2], 0, alloc_size / 2);
				}

				for (alloc_tmp = 0; bb[alloc_tmp] && bb[alloc_tmp] != '\n' &&
				alloc_tmp < alloc_size; alloc_tmp++);

				if (!bb[alloc_tmp] || alloc_tmp == alloc_size) {
					strncat(cmd, bb, alloc_tmp);
					bb += alloc_tmp;
					if (block_state == LAST_BL)
						die("Command not finished before end of file");
					else
						block_state = NEW_BL;
					break;
				} else {
					strncat(cmd, bb, alloc_tmp);
					if (!(bb[alloc_tmp - 1] == '\\'))
						parse_state = LAST;
					bb += alloc_tmp + 1;
					linenum++;
					break;
				}
				break;
			case 5:
				if (!keys)
					die("error");
				i_tmp = strlen(keys);
				for (int i = 0; i < i_tmp; i++) {
					if (isblank(keys[i])) {
						memmove(&keys[i], &keys[i + 1], --i_tmp);
						keys[i_tmp] = '\0';
						}
				}
				cp_tmp = strtok(keys, ",");
				if(!cp_tmp)
					die("Error at line %d: "
					"keys not present", linenum - 1);

				do {
					if (!(us_tmp = key_to_code(cp_tmp))) {
						die("Error at line %d: "
						"%s is not a valid key",
						linenum - 1, cp_tmp);
					}
					if (key_buffer_add(&kb, us_tmp))
						die("Too many keys");
				} while ((cp_tmp = strtok(NULL, ",")));

				cp_tmp = cmd;
				while (isblank(*cp_tmp))
					cp_tmp++;
				if (*cp_tmp == '\0')
					die("Error at line %d: "
					"command not present", linenum - 1);

				hotkey_list_add(hotkey_list, &kb, cp_tmp, fuzzy);
				hotkey_size_mask |= 1 << (kb.size - 1);

				key_buffer_reset(&kb);
				free(keys);
				free(cmd);
				cp_tmp = keys = cmd = NULL;
				i_tmp = 0;
				parse_state = NORM;
				break;
			default:
				die("Unknown state in parse_config_file");
				break;

			}
		}
	}
}

unsigned short key_to_code (char *key)
{
	for (char *tmp = key; *tmp; tmp++) {
		if (islower(*tmp))
			*tmp += 'A' - 'a';
	}
	for (int i = 0; i < array_size_const(key_conversion_table); i++) {
		if (!strcmp(key_conversion_table[i].name, key))
			return key_conversion_table[i].value;
	}
	return 0;
}

void remove_lock (void)
{
	unlink(LOCK_FILE);
}

void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	fputs(ANSI_COLOR_RED, stderr);
     	vfprintf(stderr, fmt, ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}
     	fputs(ANSI_COLOR_RESET, stderr);

	va_end(ap);
	exit(errno ? errno : 1);
}

const char * code_to_name (unsigned int code)
{
	for (int i = 0; i < array_size_const(key_conversion_table); i++) {
		if (key_conversion_table[i].value == code)
			return key_conversion_table[i].name;
	}
	return "Key not recognized";
}

void usage (void)
{
	puts("Usage: hkd [-vdh] [-c file]\n"
	     "\t-v        verbose, prints all the key presses and debug information\n"
	     "\t-d        dump, dumps the hotkey list and exits\n"
	     "\t-h        prints this help message\n"
	     "\t-f file   uses the specified file as config\n");
	exit(EXIT_SUCCESS);
}
