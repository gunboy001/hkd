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
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <wordexp.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdarg.h>

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

const struct {
	const char *const name;
	const unsigned short value;
} key_conversion_table[] =
{{"ESC", KEY_ESC}, {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3},
{"4", KEY_4}, {"5", KEY_5}, {"6", KEY_6}, {"7", KEY_7},
{"8", KEY_8}, {"9", KEY_9}, {"0", KEY_0}, {"MINUS", KEY_MINUS},
{"EQUAL", KEY_EQUAL}, {"BACKSPACE", KEY_BACKSPACE}, {"TAB", KEY_TAB},
{"Q", KEY_Q}, {"W", KEY_W}, {"E", KEY_E}, {"R", KEY_R},
{"T", KEY_T}, {"Y", KEY_Y}, {"U", KEY_U}, {"I", KEY_I},
{"O", KEY_O}, {"P", KEY_P}, {"LEFTBRACE", KEY_LEFTBRACE},
{"RIGHTBRACE", KEY_RIGHTBRACE}, {"ENTER", KEY_ENTER}, {"LEFTCTRL", KEY_LEFTCTRL},
{"A", KEY_A}, {"S", KEY_S}, {"D", KEY_D}, {"F", KEY_F},
{"G", KEY_G}, {"H", KEY_H}, {"J", KEY_J}, {"K", KEY_K},
{"L", KEY_L}, {"SEMICOLON", KEY_SEMICOLON}, {"APOSTROPHE", KEY_APOSTROPHE},
{"GRAVE", KEY_GRAVE}, {"LEFTSHIFT", KEY_LEFTSHIFT}, {"BACKSLASH", KEY_BACKSLASH},
{"Z", KEY_Z}, {"X", KEY_X}, {"C", KEY_C}, {"V", KEY_V},
{"B", KEY_B}, {"N", KEY_N}, {"M", KEY_M}, {"COMMA", KEY_COMMA},
{"DOT", KEY_DOT}, {"SLASH", KEY_SLASH}, {"RIGHTSHIFT", KEY_RIGHTSHIFT},
{"KPASTERISK", KEY_KPASTERISK}, {"LEFTALT", KEY_LEFTALT}, {"SPACE", KEY_SPACE},
{"CAPSLOCK", KEY_CAPSLOCK}, {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3},
{"F4", KEY_F4}, {"F5", KEY_F5}, {"F6", KEY_F6}, {"F7", KEY_F7},
{"F8", KEY_F8}, {"F9", KEY_F9}, {"F10", KEY_F10}, {"NUMLOCK", KEY_NUMLOCK},
{"SCROLLLOCK", KEY_SCROLLLOCK}, {"KP7", KEY_KP7}, {"KP8", KEY_KP8},
{"KP9", KEY_KP9}, {"KPMINUS", KEY_KPMINUS}, {"KP4", KEY_KP4}, {"KP5", KEY_KP5},
{"KP6", KEY_KP6}, {"KPPLUS", KEY_KPPLUS}, {"KP1", KEY_KP1}, {"KP2", KEY_KP2},
{"KP3", KEY_KP3}, {"KP0", KEY_KP0}, {"KPDOT", KEY_KP0},
{"ZENKAKUHANKAKU", KEY_ZENKAKUHANKAKU}, {"102ND", KEY_102ND}, {"F11", KEY_F11},
{"F12", KEY_F12}, {"RO", KEY_RO}, {"KATAKANA", KEY_KATAKANA},
{"HIRAGANA", KEY_HIRAGANA}, {"HENKAN", KEY_HENKAN},
{"KATAKANAHIRAGANA", KEY_KATAKANAHIRAGANA}, {"MUHENKAN", KEY_MUHENKAN},
{"KPJPCOMMA", KEY_KPJPCOMMA}, {"KPENTER", KEY_KPENTER},
{"RIGHTCTRL", KEY_RIGHTCTRL}, {"KPSLASH", KEY_KPSLASH},
{"SYSRQ", KEY_SYSRQ}, {"RIGHTALT", KEY_RIGHTALT}, {"LINEFEED", KEY_LINEFEED},
{"HOME", KEY_HOME}, {"UP", KEY_UP}, {"PAGEUP", KEY_PAGEUP},
{"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT}, {"END", KEY_END},
{"DOWN", KEY_DOWN}, {"PAGEDOWN", KEY_PAGEDOWN}, {"INSERT", KEY_INSERT},
{"DELETE", KEY_DELETE}, {"MACRO", KEY_MACRO}, {"MUTE", KEY_MUTE},
{"VOLUMEDOWN", KEY_VOLUMEDOWN}, {"VOLUMEUP", KEY_VOLUMEUP},
{"POWER", KEY_POWER}, {"KPEQUAL", KEY_KPEQUAL}, {"KPPLUSMINUS", KEY_KPPLUSMINUS},
{"PAUSE", KEY_PAUSE}, {"SCALE", KEY_SCALE}, {"KPCOMMA", KEY_KPCOMMA},
{"HANGEUL", KEY_HANGEUL}, {"HANGUEL", KEY_HANGEUL}, {"HANJA", KEY_HANJA},
{"YEN", KEY_YEN}, {"LEFTMETA", KEY_LEFTMETA}, {"RIGHTMETA", KEY_LEFTMETA},
{"COMPOSE", KEY_COMPOSE}, {"STOP", KEY_STOP}, {"AGAIN", KEY_AGAIN},
{"PROPS", KEY_PROPS}, {"UNDO", KEY_UNDO}, {"FRONT", KEY_FRONT},
{"COPY", KEY_COPY}, {"OPEN", KEY_OPEN}, {"PASTE", KEY_PASTE},
{"FIND", KEY_FIND}, {"CUT", KEY_CUT}, {"HELP", KEY_HELP},
{"MENU", KEY_MENU}, {"CALC", KEY_CALC}, {"SETUP", KEY_SETUP},
{"SLEEP", KEY_SLEEP}, {"WAKEUP", KEY_WAKEUP}, {"FILE", KEY_FILE},
{"SENDFILE", KEY_SENDFILE}, {"DELETEFILE", KEY_DELETEFILE},
{"XFER", KEY_XFER}, {"PROG1", KEY_PROG1}, {"PROG2", KEY_PROG2},
{"WWW", KEY_WWW}, {"MSDOS", KEY_MSDOS}, {"COFFEE", KEY_COFFEE},
{"SCREENLOCK", KEY_COFFEE}, {"DIRECTION", KEY_DIRECTION},
{"CYCLEWINDOWS", KEY_CYCLEWINDOWS}, {"MAIL", KEY_MAIL},
{"BOOKMARKS", KEY_BOOKMARKS}, {"COMPUTER", KEY_COMPUTER}, {"BACK", KEY_BACK},
{"FORWARD", KEY_FORWARD}, {"CLOSECD", KEY_CLOSECD}, {"EJECTCD", KEY_EJECTCD},
{"EJECTCLOSECD", KEY_EJECTCLOSECD}, {"NEXTSONG", KEY_NEXTSONG},
{"PLAYPAUSE", KEY_PLAYPAUSE}, {"PREVIOUSSONG", KEY_PREVIOUSSONG},
{"STOPCD", KEY_STOPCD}, {"RECORD", KEY_RECORD}, {"REWIND", KEY_REWIND},
{"PHONE", KEY_PHONE}, {"ISO", KEY_ISO}, {"CONFIG", KEY_CONFIG},
{"HOMEPAGE", KEY_HOMEPAGE}, {"REFRESH", KEY_REFRESH}, {"EXIT", KEY_EXIT},
{"MOVE", KEY_MOVE}, {"EDIT", KEY_EDIT}, {"SCROLLUP", KEY_SCROLLUP},
{"SCROLLDOWN", KEY_SCROLLDOWN}, {"KPLEFTPAREN", KEY_KPLEFTPAREN},
{"KPRIGHTPAREN", KEY_KPRIGHTPAREN}, {"NEW", KEY_NEW}, {"REDO", KEY_REDO},
{"F13", KEY_F13}, {"F14", KEY_F14}, {"F15", KEY_F15}, {"F16", KEY_F16},
{"F17", KEY_F17}, {"F18", KEY_F18}, {"F19", KEY_F19}, {"F20", KEY_F20},
{"F21", KEY_F21}, {"F22", KEY_F22}, {"F23", KEY_F23}, {"F24", KEY_F24},
{"PLAYCD", KEY_PLAYCD}, {"PAUSECD", KEY_PAUSECD}, {"PROG3", KEY_PROG3},
{"PROG4", KEY_PROG4}, {"DASHBOARD", KEY_DASHBOARD}, {"SUSPEND", KEY_SUSPEND},
{"CLOSE", KEY_CLOSE}, {"PLAY", KEY_PLAY}, {"FASTFORWARD", KEY_FASTFORWARD},
{"BASSBOOST", KEY_BASSBOOST}, {"PRINT", KEY_PRINT}, {"HP", KEY_HP},
{"CAMERA", KEY_CAMERA}, {"SOUND", KEY_SOUND}, {"QUESTION", KEY_QUESTION},
{"EMAIL", KEY_EMAIL}, {"CHAT", KEY_CHAT}, {"SEARCH", KEY_SEARCH},
{"CONNECT", KEY_CONNECT}, {"FINANCE", KEY_FINANCE}, {"SPORT", KEY_SPORT},
{"SHOP", KEY_SHOP}, {"ALTERASE", KEY_ALTERASE}, {"CANCEL", KEY_CANCEL},
{"BRIGHTNESSDOWN", KEY_BRIGHTNESSDOWN}, {"BRIGHTNESSUP", KEY_BRIGHTNESSUP},
{"MEDIA", KEY_MEDIA}, {"SWITCHVIDEOMODE", KEY_SWITCHVIDEOMODE},
{"KBDILLUMTOGGLE", KEY_KBDILLUMTOGGLE}, {"KBDILLUMDOWN", KEY_KBDILLUMDOWN},
{"KBDILLUMUP", KEY_KBDILLUMUP}, {"SEND", KEY_SEND}, {"REPLY", KEY_REPLY},
{"FORWARDMAIL", KEY_FORWARDMAIL}, {"SAVE", KEY_SAVE},
{"DOCUMENTS", KEY_DOCUMENTS}, {"BATTERY", KEY_BATTERY},
{"BLUETOOTH", KEY_BLUETOOTH}, {"WLAN", KEY_WLAN}, {"UWB", KEY_UWB},
{"UNKNOWN", KEY_UNKNOWN}, {"VIDEO_NEXT", KEY_VIDEO_NEXT},
{"VIDEO_PREV", KEY_VIDEO_PREV}, {"BRIGHTNESS_CYCLE", KEY_BRIGHTNESS_CYCLE},
{"BRIGHTNESS_ZERO", KEY_BRIGHTNESS_ZERO}, {"DISPLAY_OFF", KEY_DISPLAY_OFF},
{"WIMAX", KEY_WIMAX}, {"RFKILL", KEY_RFKILL}, {"OK", KEY_OK},
{"SELECT", KEY_SELECT}, {"GOTO", KEY_GOTO}, {"CLEAR", KEY_CLEAR},
{"POWER2", KEY_POWER2}, {"OPTION", KEY_OPTION}, {"INFO", KEY_INFO},
{"TIME", KEY_TIME}, {"VENDOR", KEY_VENDOR}, {"ARCHIVE", KEY_ARCHIVE},
{"PROGRAM", KEY_PROGRAM}, {"CHANNEL", KEY_CHANNEL}, {"FAVORITES", KEY_FAVORITES},
{"EPG", KEY_EPG}, {"PVR", KEY_PVR}, {"MHP", KEY_MHP},
{"LANGUAGE", KEY_LANGUAGE}, {"TITLE", KEY_TITLE}, {"SUBTITLE", KEY_SUBTITLE},
{"ANGLE", KEY_ANGLE}, {"ZOOM", KEY_ZOOM}, {"MODE", KEY_MODE},
{"KEYBOARD", KEY_KEYBOARD}, {"SCREEN", KEY_SCREEN}, {"PC", KEY_PC},
{"TV", KEY_TV}, {"TV2", KEY_TV2}, {"VCR", KEY_VCR}, {"VCR2", KEY_VCR2},
{"SAT", KEY_SAT}, {"SAT2", KEY_SAT2}, {"CD", KEY_CD}, {"TAPE", KEY_TAPE},
{"RADIO", KEY_RADIO}, {"TUNER", KEY_TUNER}, {"PLAYER", KEY_PLAYER},
{"TEXT", KEY_TEXT}, {"DVD", KEY_DVD}, {"AUX", KEY_AUX}, {"MP3", KEY_MP3},
{"AUDIO", KEY_AUDIO}, {"VIDEO", KEY_VIDEO}, {"DIRECTORY", KEY_DIRECTORY},
{"LIST", KEY_LIST}, {"MEMO", KEY_MEMO}, {"CALENDAR", KEY_CALENDAR},
{"RED", KEY_RED}, {"GREEN", KEY_GREEN}, {"YELLOW", KEY_YELLOW},
{"BLUE", KEY_BLUE}, {"CHANNELUP", KEY_CHANNELUP},
{"CHANNELDOWN", KEY_CHANNELDOWN}, {"FIRST", KEY_FIRST}, {"LAST", KEY_LAST},
{"AB", KEY_AB}, {"NEXT", KEY_NEXT}, {"RESTART", KEY_RESTART},
{"SLOW", KEY_SLOW}, {"SHUFFLE", KEY_SHUFFLE}, {"BREAK", KEY_BREAK},
{"PREVIOUS", KEY_PREVIOUS}, {"DIGITS", KEY_DIGITS}, {"TEEN", KEY_TEEN},
{"TWEN", KEY_TWEN}, {"VIDEOPHONE", KEY_VIDEOPHONE}, {"GAMES", KEY_GAMES},
{"ZOOMIN", KEY_ZOOMIN}, {"ZOOMOUT", KEY_ZOOMOUT}, {"ZOOMRESET", KEY_ZOOMRESET},
{"WORDPROCESSOR", KEY_WORDPROCESSOR}, {"EDITOR", KEY_EDITOR},
{"SPREADSHEET", KEY_SPREADSHEET}, {"GRAPHICSEDITOR", KEY_GRAPHICSEDITOR},
{"PRESENTATION", KEY_PRESENTATION}, {"DATABASE", KEY_DATABASE},
{"NEWS", KEY_NEWS}, {"VOICEMAIL", KEY_VOICEMAIL},
{"ADDRESSBOOK", KEY_ADDRESSBOOK}, {"MESSENGER", KEY_MESSENGER},
{"DISPLAYTOGGLE", KEY_DISPLAYTOGGLE}, {"SPELLCHECK", KEY_SPELLCHECK},
{"LOGOFF", KEY_LOGOFF}, {"DOLLAR", KEY_DOLLAR}, {"EURO", KEY_EURO},
{"FRAMEBACK", KEY_FRAMEBACK}, {"FRAMEFORWARD", KEY_FRAMEFORWARD},
{"CONTEXT_MENU", KEY_CONTEXT_MENU}, {"MEDIA_REPEAT", KEY_MEDIA_REPEAT},
{"10CHANNELSUP", KEY_10CHANNELSUP}, {"10CHANNELSDOWN", KEY_10CHANNELSDOWN},
{"DEL_EOL", KEY_DEL_EOL}, {"DEL_EOS", KEY_DEL_EOS}, {"INS_LINE", KEY_INS_LINE},
{"DEL_LINE", KEY_DEL_LINE}, {"FN", KEY_FN}, {"FN_ESC", KEY_FN_ESC},
{"FN_F1", KEY_FN_F1}, {"FN_F2", KEY_FN_F2}, {"FN_F3", KEY_FN_F3},
{"FN_F4", KEY_FN_F4}, {"FN_F5", KEY_FN_F5}, {"FN_F6", KEY_FN_F6},
{"FN_F7", KEY_FN_F7}, {"FN_F8", KEY_FN_F8}, {"FN_F9", KEY_FN_F9},
{"FN_F10", KEY_FN_F10}, {"FN_F11", KEY_FN_F11}, {"FN_F12", KEY_FN_F12},
{"FN_1", KEY_FN_1}, {"FN_2", KEY_FN_2}, {"FN_D", KEY_FN_D},
{"FN_E", KEY_FN_E}, {"FN_F", KEY_FN_F}, {"FN_S", KEY_FN_S},
{"FN_B", KEY_FN_B}, {"BRL_DOT1", KEY_BRL_DOT1}, {"BRL_DOT2", KEY_BRL_DOT2},
{"BRL_DOT3", KEY_BRL_DOT3}, {"BRL_DOT4", KEY_BRL_DOT4},
{"BRL_DOT5", KEY_BRL_DOT5}, {"BRL_DOT6", KEY_BRL_DOT6},
{"BRL_DOT7", KEY_BRL_DOT7}, {"BRL_DOT8", KEY_BRL_DOT8},
{"BRL_DOT9", KEY_BRL_DOT9}, {"BRL_DOT10", KEY_BRL_DOT10},
{"NUMERIC_0", KEY_NUMERIC_0}, {"NUMERIC_1", KEY_NUMERIC_1},
{"NUMERIC_2", KEY_NUMERIC_2}, {"NUMERIC_3", KEY_NUMERIC_3},
{"NUMERIC_4", KEY_NUMERIC_4}, {"NUMERIC_5", KEY_NUMERIC_5},
{"NUMERIC_6", KEY_NUMERIC_6}, {"NUMERIC_7", KEY_NUMERIC_7},
{"NUMERIC_8", KEY_NUMERIC_8}, {"NUMERIC_9", KEY_NUMERIC_9},
{"NUMERIC_STAR", KEY_NUMERIC_STAR}, {"NUMERIC_POUND", KEY_NUMERIC_POUND},
{"CAMERA_FOCUS", KEY_CAMERA_FOCUS}, {"WPS_BUTTON", KEY_WPS_BUTTON},
{"TOUCHPAD_TOGGLE", KEY_TOUCHPAD_TOGGLE}, {"TOUCHPAD_ON", KEY_TOUCHPAD_ON},
{"TOUCHPAD_OFF", KEY_TOUCHPAD_OFF}};

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
/* Flags */
int vflag = 0;
int dead = 0; // exit flag
int dump = 0;
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
	ssize_t rb; // Read bits
	struct flock fl;
	struct sigaction action;
	struct input_event event;
	struct key_buffer pb = {{0}, 0}; // Pressed keys buffer

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

			rb = read(fds[i], &event, sizeof(struct input_event));
			if (rb != sizeof(struct input_event)) continue;

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
	// 0: normal, 1: skip line 2: get directive 3: get keys 4: get command 5: output
	int state = 0;
	int alloc_tmp = 0, alloc_size = 0;
	int fuzzy = 0;
	int i_tmp = 0, linenum = 1;
	// 0: aok, 1: request block, 2: last block, -1: exit
	int exit_state = 0;
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
	while (exit_state >= 0) {
		int tmp = 0;
		memset(block, 0, BLOCK_SIZE + 1);
		tmp = fread(block, sizeof(char), BLOCK_SIZE, fd);
		if (!tmp)
			break;
		if (tmp < BLOCK_SIZE || feof(fd))
			exit_state = 2;
		else
			exit_state = 0;
		bb = block;

		while (exit_state == 0 || exit_state == 2) {
			switch (state) {
			// First state
			case 0:
				// remove whitespaces
				while (isblank(*bb))
					bb++;
				// get state
				switch (*bb) {
				case EOF:
				case '\0':
					// If it is the end of the last block exit
					if (exit_state > 1)
						exit_state = -1;
					break;
				case '\n':
				case '#':
					state = 1;
					break;
				default:
					state = 2;
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
					state = 0;
				} else {
					exit_state = 1;
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
				state = 3;
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
					if (exit_state > 1)
						die("Keys not finished before end of file");
					else
						exit_state = 1;
					break;
				} else if (bb[alloc_tmp] == ':') {
					strncat(keys, bb, alloc_tmp);
					bb += alloc_tmp + 1;
					state = 4;
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
					if (exit_state > 1)
						die("Command not finished before end of file");
					else
						exit_state = 1;
					break;
				} else {
					strncat(cmd, bb, alloc_tmp);
					if (!(bb[alloc_tmp - 1] == '\\'))
						state = 5;
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
				state = 0;
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
