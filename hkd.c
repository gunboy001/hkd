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

#define FILE_NAME_MAX_LENGTH 255
#define KEY_BUFFER_SIZE 16

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define yellow(str) (ANSI_COLOR_YELLOW str ANSI_COLOR_RESET)
#define green(str) (ANSI_COLOR_GREEN str ANSI_COLOR_RESET)
#define red(str) (ANSI_COLOR_RED str ANSI_COLOR_RESET)
#define test_bit(yalv, abs_b) ((((char *)abs_b)[yalv/8] & (1<<yalv%8)) > 0)
#define die(str) {perror(red(str)); exit(errno);}
#define array_size(val) (val ? sizeof(val)/sizeof(val[0]) : 0)

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024*(EVENT_SIZE+16))

const struct {
	const char * const name;
	const unsigned short value;
} key_conversion_table[] =
{{"KEY_ESC", KEY_ESC}, {"KEY_1", KEY_1}, {"KEY_2", KEY_2}, {"KEY_3", KEY_3},
{"KEY_4", KEY_4}, {"KEY_5", KEY_5}, {"KEY_6", KEY_6}, {"KEY_7", KEY_7},
{"KEY_8", KEY_8}, {"KEY_9", KEY_9}, {"KEY_0", KEY_0}, {"KEY_MINUS", KEY_MINUS},
{"KEY_EQUAL", KEY_EQUAL}, {"KEY_BACKSPACE", KEY_BACKSPACE}, {"KEY_TAB", KEY_TAB},
{"KEY_Q", KEY_Q}, {"KEY_W", KEY_W}, {"KEY_E", KEY_E}, {"KEY_R", KEY_R},
{"KEY_T", KEY_T}, {"KEY_Y", KEY_Y}, {"KEY_U", KEY_U}, {"KEY_I", KEY_I},
{"KEY_O", KEY_O}, {"KEY_P", KEY_P}, {"KEY_LEFTBRACE", KEY_LEFTBRACE},
{"KEY_RIGHTBRACE", KEY_RIGHTBRACE}, {"KEY_ENTER", KEY_ENTER}, {"KEY_LEFTCTRL", KEY_LEFTCTRL},
{"KEY_A", KEY_A}, {"KEY_S", KEY_S}, {"KEY_D", KEY_D}, {"KEY_F", KEY_F},
{"KEY_G", KEY_G}, {"KEY_H", KEY_H}, {"KEY_J", KEY_J}, {"KEY_K", KEY_K},
{"KEY_L", KEY_L}, {"KEY_SEMICOLON", KEY_SEMICOLON}, {"KEY_APOSTROPHE", KEY_APOSTROPHE},
{"KEY_GRAVE", KEY_GRAVE}, {"KEY_LEFTSHIFT", KEY_LEFTSHIFT}, {"KEY_BACKSLASH", KEY_BACKSLASH},
{"KEY_Z", KEY_Z}, {"KEY_X", KEY_X}, {"KEY_C", KEY_C}, {"KEY_V", KEY_V},
{"KEY_B", KEY_B}, {"KEY_N", KEY_N}, {"KEY_M", KEY_M}, {"KEY_COMMA", KEY_COMMA},
{"KEY_DOT", KEY_DOT}, {"KEY_SLASH", KEY_SLASH}, {"KEY_RIGHTSHIFT", KEY_RIGHTSHIFT},
{"KEY_KPASTERISK", KEY_KPASTERISK}, {"KEY_LEFTALT", KEY_LEFTALT}, {"KEY_SPACE", KEY_SPACE},
{"KEY_CAPSLOCK", KEY_CAPSLOCK}, {"KEY_F1", KEY_F1}, {"KEY_F2", KEY_F2}, {"KEY_F3", KEY_F3},
{"KEY_F4", KEY_F4}, {"KEY_F5", KEY_F5}, {"KEY_F6", KEY_F6}, {"KEY_F7", KEY_F7},
{"KEY_F8", KEY_F8}, {"KEY_F9", KEY_F9}, {"KEY_F10", KEY_F10}, {"KEY_NUMLOCK", KEY_NUMLOCK},
{"KEY_SCROLLLOCK", KEY_SCROLLLOCK}, {"KEY_KP7", KEY_KP7}, {"KEY_KP8", KEY_KP8},
{"KEY_KP9", KEY_KP9}, {"KEY_KPMINUS", KEY_KPMINUS}, {"KEY_KP4", KEY_KP4}, {"KEY_KP5", KEY_KP5},
{"KEY_KP6", KEY_KP6}, {"KEY_KPPLUS", KEY_KPPLUS}, {"KEY_KP1", KEY_KP1}, {"KEY_KP2", KEY_KP2},
{"KEY_KP3", KEY_KP3}, {"KEY_KP0", KEY_KP0}, {"KEY_KPDOT", KEY_KP0},
{"KEY_ZENKAKUHANKAKU", KEY_ZENKAKUHANKAKU}, {"KEY_102ND", KEY_102ND}, {"KEY_F11", KEY_F11},
{"KEY_F12", KEY_F12}, {"KEY_RO", KEY_RO}, {"KEY_KATAKANA", KEY_KATAKANA},
{"KEY_HIRAGANA", KEY_HIRAGANA}, {"KEY_HENKAN", KEY_HENKAN},
{"KEY_KATAKANAHIRAGANA", KEY_KATAKANAHIRAGANA}, {"KEY_MUHENKAN", KEY_MUHENKAN},
{"KEY_KPJPCOMMA", KEY_KPJPCOMMA}, {"KEY_KPENTER", KEY_KPENTER},
{"KEY_RIGHTCTRL", KEY_RIGHTCTRL}, {"KEY_KPSLASH", KEY_KPSLASH},
{"KEY_SYSRQ", KEY_SYSRQ}, {"KEY_RIGHTALT", KEY_RIGHTALT}, {"KEY_LINEFEED", KEY_LINEFEED},
{"KEY_HOME", KEY_HOME}, {"KEY_UP", KEY_UP}, {"KEY_PAGEUP", KEY_PAGEUP},
{"KEY_LEFT", KEY_LEFT}, {"KEY_RIGHT", KEY_RIGHT}, {"KEY_END", KEY_END},
{"KEY_DOWN", KEY_DOWN}, {"KEY_PAGEDOWN", KEY_PAGEDOWN}, {"KEY_INSERT", KEY_INSERT},
{"KEY_DELETE", KEY_DELETE}, {"KEY_MACRO", KEY_MACRO}, {"KEY_MUTE", KEY_MUTE},
{"KEY_VOLUMEDOWN", KEY_VOLUMEDOWN}, {"KEY_VOLUMEUP", KEY_VOLUMEUP},
{"KEY_POWER", KEY_POWER}, {"KEY_KPEQUAL", KEY_KPEQUAL}, {"KEY_KPPLUSMINUS", KEY_KPPLUSMINUS},
{"KEY_PAUSE", KEY_PAUSE}, {"KEY_SCALE", KEY_SCALE}, {"KEY_KPCOMMA", KEY_KPCOMMA},
{"KEY_HANGEUL", KEY_HANGEUL}, {"KEY_HANGUEL", KEY_HANGEUL}, {"KEY_HANJA", KEY_HANJA},
{"KEY_YEN", KEY_YEN}, {"KEY_LEFTMETA", KEY_LEFTMETA}, {"KEY_RIGHTMETA", KEY_LEFTMETA},
{"KEY_COMPOSE", KEY_COMPOSE}, {"KEY_STOP", KEY_STOP}, {"KEY_AGAIN", KEY_AGAIN},
{"KEY_PROPS", KEY_PROPS}, {"KEY_UNDO", KEY_UNDO}, {"KEY_FRONT", KEY_FRONT},
{"KEY_COPY", KEY_COPY}, {"KEY_OPEN", KEY_OPEN}, {"KEY_PASTE", KEY_PASTE},
{"KEY_FIND", KEY_FIND}, {"KEY_CUT", KEY_CUT}, {"KEY_HELP", KEY_HELP},
{"KEY_MENU", KEY_MENU}, {"KEY_CALC", KEY_CALC}, {"KEY_SETUP", KEY_SETUP},
{"KEY_SLEEP", KEY_SLEEP}, {"KEY_WAKEUP", KEY_WAKEUP}, {"KEY_FILE", KEY_FILE},
{"KEY_SENDFILE", KEY_SENDFILE}, {"KEY_DELETEFILE", KEY_DELETEFILE},
{"KEY_XFER", KEY_XFER}, {"KEY_PROG1", KEY_PROG1}, {"KEY_PROG2", KEY_PROG2},
{"KEY_WWW", KEY_WWW}, {"KEY_MSDOS", KEY_MSDOS}, {"KEY_COFFEE", KEY_COFFEE},
{"KEY_SCREENLOCK", KEY_COFFEE}, {"KEY_DIRECTION", KEY_DIRECTION},
{"KEY_CYCLEWINDOWS", KEY_CYCLEWINDOWS}, {"KEY_MAIL", KEY_MAIL},
{"KEY_BOOKMARKS", KEY_BOOKMARKS}, {"KEY_COMPUTER", KEY_COMPUTER}, {"KEY_BACK", KEY_BACK},
{"KEY_FORWARD", KEY_FORWARD}, {"KEY_CLOSECD", KEY_CLOSECD}, {"KEY_EJECTCD", KEY_EJECTCD},
{"KEY_EJECTCLOSECD", KEY_EJECTCLOSECD}, {"KEY_NEXTSONG", KEY_NEXTSONG},
{"KEY_PLAYPAUSE", KEY_PLAYPAUSE}, {"KEY_PREVIOUSSONG", KEY_PREVIOUSSONG},
{"KEY_STOPCD", KEY_STOPCD}, {"KEY_RECORD", KEY_RECORD}, {"KEY_REWIND", KEY_REWIND},
{"KEY_PHONE", KEY_PHONE}, {"KEY_ISO", KEY_ISO}, {"KEY_CONFIG", KEY_CONFIG},
{"KEY_HOMEPAGE", KEY_HOMEPAGE}, {"KEY_REFRESH", KEY_REFRESH}, {"KEY_EXIT", KEY_EXIT},
{"KEY_MOVE", KEY_MOVE}, {"KEY_EDIT", KEY_EDIT}, {"KEY_SCROLLUP", KEY_SCROLLUP},
{"KEY_SCROLLDOWN", KEY_SCROLLDOWN}, {"KEY_KPLEFTPAREN", KEY_KPLEFTPAREN},
{"KEY_KPRIGHTPAREN", KEY_KPRIGHTPAREN}, {"KEY_NEW", KEY_NEW}, {"KEY_REDO", KEY_REDO},
{"KEY_F13", KEY_F13}, {"KEY_F14", KEY_F14}, {"KEY_F15", KEY_F15}, {"KEY_F16", KEY_F16},
{"KEY_F17", KEY_F17}, {"KEY_F18", KEY_F18}, {"KEY_F19", KEY_F19}, {"KEY_F20", KEY_F20},
{"KEY_F21", KEY_F21}, {"KEY_F22", KEY_F22}, {"KEY_F23", KEY_F23}, {"KEY_F24", KEY_F24},
{"KEY_PLAYCD", KEY_PLAYCD}, {"KEY_PAUSECD", KEY_PAUSECD}, {"KEY_PROG3", KEY_PROG3},
{"KEY_PROG4", KEY_PROG4}, {"KEY_DASHBOARD", KEY_DASHBOARD}, {"KEY_SUSPEND", KEY_SUSPEND},
{"KEY_CLOSE", KEY_CLOSE}, {"KEY_PLAY", KEY_PLAY}, {"KEY_FASTFORWARD", KEY_FASTFORWARD},
{"KEY_BASSBOOST", KEY_BASSBOOST}, {"KEY_PRINT", KEY_PRINT}, {"KEY_HP", KEY_HP},
{"KEY_CAMERA", KEY_CAMERA}, {"KEY_SOUND", KEY_SOUND}, {"KEY_QUESTION", KEY_QUESTION},
{"KEY_EMAIL", KEY_EMAIL}, {"KEY_CHAT", KEY_CHAT}, {"KEY_SEARCH", KEY_SEARCH},
{"KEY_CONNECT", KEY_CONNECT}, {"KEY_FINANCE", KEY_FINANCE}, {"KEY_SPORT", KEY_SPORT},
{"KEY_SHOP", KEY_SHOP}, {"KEY_ALTERASE", KEY_ALTERASE}, {"KEY_CANCEL", KEY_CANCEL},
{"KEY_BRIGHTNESSDOWN", KEY_BRIGHTNESSDOWN}, {"KEY_BRIGHTNESSUP", KEY_BRIGHTNESSUP},
{"KEY_MEDIA", KEY_MEDIA}, {"KEY_SWITCHVIDEOMODE", KEY_SWITCHVIDEOMODE},
{"KEY_KBDILLUMTOGGLE", KEY_KBDILLUMTOGGLE}, {"KEY_KBDILLUMDOWN", KEY_KBDILLUMDOWN},
{"KEY_KBDILLUMUP", KEY_KBDILLUMUP}, {"KEY_SEND", KEY_SEND}, {"KEY_REPLY", KEY_REPLY},
{"KEY_FORWARDMAIL", KEY_FORWARDMAIL}, {"KEY_SAVE", KEY_SAVE},
{"KEY_DOCUMENTS", KEY_DOCUMENTS}, {"KEY_BATTERY", KEY_BATTERY},
{"KEY_BLUETOOTH", KEY_BLUETOOTH}, {"KEY_WLAN", KEY_WLAN}, {"KEY_UWB", KEY_UWB},
{"KEY_UNKNOWN", KEY_UNKNOWN}, {"KEY_VIDEO_NEXT", KEY_VIDEO_NEXT},
{"KEY_VIDEO_PREV", KEY_VIDEO_PREV}, {"KEY_BRIGHTNESS_CYCLE", KEY_BRIGHTNESS_CYCLE},
{"KEY_BRIGHTNESS_ZERO", KEY_BRIGHTNESS_ZERO}, {"KEY_DISPLAY_OFF", KEY_DISPLAY_OFF},
{"KEY_WIMAX", KEY_WIMAX}, {"KEY_RFKILL", KEY_RFKILL}, {"KEY_OK", KEY_OK},
{"KEY_SELECT", KEY_SELECT}, {"KEY_GOTO", KEY_GOTO}, {"KEY_CLEAR", KEY_CLEAR},
{"KEY_POWER2", KEY_POWER2}, {"KEY_OPTION", KEY_OPTION}, {"KEY_INFO", KEY_INFO},
{"KEY_TIME", KEY_TIME}, {"KEY_VENDOR", KEY_VENDOR}, {"KEY_ARCHIVE", KEY_ARCHIVE},
{"KEY_PROGRAM", KEY_PROGRAM}, {"KEY_CHANNEL", KEY_CHANNEL}, {"KEY_FAVORITES", KEY_FAVORITES},
{"KEY_EPG", KEY_EPG}, {"KEY_PVR", KEY_PVR}, {"KEY_MHP", KEY_MHP},
{"KEY_LANGUAGE", KEY_LANGUAGE}, {"KEY_TITLE", KEY_TITLE}, {"KEY_SUBTITLE", KEY_SUBTITLE},
{"KEY_ANGLE", KEY_ANGLE}, {"KEY_ZOOM", KEY_ZOOM}, {"KEY_MODE", KEY_MODE},
{"KEY_KEYBOARD", KEY_KEYBOARD}, {"KEY_SCREEN", KEY_SCREEN}, {"KEY_PC", KEY_PC},
{"KEY_TV", KEY_TV}, {"KEY_TV2", KEY_TV2}, {"KEY_VCR", KEY_VCR}, {"KEY_VCR2", KEY_VCR2},
{"KEY_SAT", KEY_SAT}, {"KEY_SAT2", KEY_SAT2}, {"KEY_CD", KEY_CD}, {"KEY_TAPE", KEY_TAPE},
{"KEY_RADIO", KEY_RADIO}, {"KEY_TUNER", KEY_TUNER}, {"KEY_PLAYER", KEY_PLAYER},
{"KEY_TEXT", KEY_TEXT}, {"KEY_DVD", KEY_DVD}, {"KEY_AUX", KEY_AUX}, {"KEY_MP3", KEY_MP3},
{"KEY_AUDIO", KEY_AUDIO}, {"KEY_VIDEO", KEY_VIDEO}, {"KEY_DIRECTORY", KEY_DIRECTORY},
{"KEY_LIST", KEY_LIST}, {"KEY_MEMO", KEY_MEMO}, {"KEY_CALENDAR", KEY_CALENDAR},
{"KEY_RED", KEY_RED}, {"KEY_GREEN", KEY_GREEN}, {"KEY_YELLOW", KEY_YELLOW},
{"KEY_BLUE", KEY_BLUE}, {"KEY_CHANNELUP", KEY_CHANNELUP},
{"KEY_CHANNELDOWN", KEY_CHANNELDOWN}, {"KEY_FIRST", KEY_FIRST}, {"KEY_LAST", KEY_LAST},
{"KEY_AB", KEY_AB}, {"KEY_NEXT", KEY_NEXT}, {"KEY_RESTART", KEY_RESTART},
{"KEY_SLOW", KEY_SLOW}, {"KEY_SHUFFLE", KEY_SHUFFLE}, {"KEY_BREAK", KEY_BREAK},
{"KEY_PREVIOUS", KEY_PREVIOUS}, {"KEY_DIGITS", KEY_DIGITS}, {"KEY_TEEN", KEY_TEEN},
{"KEY_TWEN", KEY_TWEN}, {"KEY_VIDEOPHONE", KEY_VIDEOPHONE}, {"KEY_GAMES", KEY_GAMES},
{"KEY_ZOOMIN", KEY_ZOOMIN}, {"KEY_ZOOMOUT", KEY_ZOOMOUT}, {"KEY_ZOOMRESET", KEY_ZOOMRESET},
{"KEY_WORDPROCESSOR", KEY_WORDPROCESSOR}, {"KEY_EDITOR", KEY_EDITOR},
{"KEY_SPREADSHEET", KEY_SPREADSHEET}, {"KEY_GRAPHICSEDITOR", KEY_GRAPHICSEDITOR},
{"KEY_PRESENTATION", KEY_PRESENTATION}, {"KEY_DATABASE", KEY_DATABASE},
{"KEY_NEWS", KEY_NEWS}, {"KEY_VOICEMAIL", KEY_VOICEMAIL},
{"KEY_ADDRESSBOOK", KEY_ADDRESSBOOK}, {"KEY_MESSENGER", KEY_MESSENGER},
{"KEY_DISPLAYTOGGLE", KEY_DISPLAYTOGGLE}, {"KEY_SPELLCHECK", KEY_SPELLCHECK},
{"KEY_LOGOFF", KEY_LOGOFF}, {"KEY_DOLLAR", KEY_DOLLAR}, {"KEY_EURO", KEY_EURO},
{"KEY_FRAMEBACK", KEY_FRAMEBACK}, {"KEY_FRAMEFORWARD", KEY_FRAMEFORWARD},
{"KEY_CONTEXT_MENU", KEY_CONTEXT_MENU}, {"KEY_MEDIA_REPEAT", KEY_MEDIA_REPEAT},
{"KEY_10CHANNELSUP", KEY_10CHANNELSUP}, {"KEY_10CHANNELSDOWN", KEY_10CHANNELSDOWN},
{"KEY_DEL_EOL", KEY_DEL_EOL}, {"KEY_DEL_EOS", KEY_DEL_EOS}, {"KEY_INS_LINE", KEY_INS_LINE},
{"KEY_DEL_LINE", KEY_DEL_LINE}, {"KEY_FN", KEY_FN}, {"KEY_FN_ESC", KEY_FN_ESC},
{"KEY_FN_F1", KEY_FN_F1}, {"KEY_FN_F2", KEY_FN_F2}, {"KEY_FN_F3", KEY_FN_F3},
{"KEY_FN_F4", KEY_FN_F4}, {"KEY_FN_F5", KEY_FN_F5}, {"KEY_FN_F6", KEY_FN_F6},
{"KEY_FN_F7", KEY_FN_F7}, {"KEY_FN_F8", KEY_FN_F8}, {"KEY_FN_F9", KEY_FN_F9},
{"KEY_FN_F10", KEY_FN_F10}, {"KEY_FN_F11", KEY_FN_F11}, {"KEY_FN_F12", KEY_FN_F12},
{"KEY_FN_1", KEY_FN_1}, {"KEY_FN_2", KEY_FN_2}, {"KEY_FN_D", KEY_FN_D},
{"KEY_FN_E", KEY_FN_E}, {"KEY_FN_F", KEY_FN_F}, {"KEY_FN_S", KEY_FN_S},
{"KEY_FN_B", KEY_FN_B}, {"KEY_BRL_DOT1", KEY_BRL_DOT1}, {"KEY_BRL_DOT2", KEY_BRL_DOT2},
{"KEY_BRL_DOT3", KEY_BRL_DOT3}, {"KEY_BRL_DOT4", KEY_BRL_DOT4},
{"KEY_BRL_DOT5", KEY_BRL_DOT5}, {"KEY_BRL_DOT6", KEY_BRL_DOT6},
{"KEY_BRL_DOT7", KEY_BRL_DOT7}, {"KEY_BRL_DOT8", KEY_BRL_DOT8},
{"KEY_BRL_DOT9", KEY_BRL_DOT9}, {"KEY_BRL_DOT10", KEY_BRL_DOT10},
{"KEY_NUMERIC_0", KEY_NUMERIC_0}, {"KEY_NUMERIC_1", KEY_NUMERIC_1},
{"KEY_NUMERIC_2", KEY_NUMERIC_2}, {"KEY_NUMERIC_3", KEY_NUMERIC_3},
{"KEY_NUMERIC_4", KEY_NUMERIC_4}, {"KEY_NUMERIC_5", KEY_NUMERIC_5},
{"KEY_NUMERIC_6", KEY_NUMERIC_6}, {"KEY_NUMERIC_7", KEY_NUMERIC_7},
{"KEY_NUMERIC_8", KEY_NUMERIC_8}, {"KEY_NUMERIC_9", KEY_NUMERIC_9},
{"KEY_NUMERIC_STAR", KEY_NUMERIC_STAR}, {"KEY_NUMERIC_POUND", KEY_NUMERIC_POUND},
{"KEY_CAMERA_FOCUS", KEY_CAMERA_FOCUS}, {"KEY_WPS_BUTTON", KEY_WPS_BUTTON},
{"KEY_TOUCHPAD_TOGGLE", KEY_TOUCHPAD_TOGGLE}, {"KEY_TOUCHPAD_ON", KEY_TOUCHPAD_ON},
{"KEY_TOUCHPAD_OFF", KEY_TOUCHPAD_OFF}};

struct key_buffer {
	unsigned short buf[KEY_BUFFER_SIZE];
	unsigned int size;
};

int dead = 0; // exit flag
const char evdev_root_dir[] = "/dev/input/";

int key_buffer_add (struct key_buffer*, unsigned short);
int key_buffer_remove (struct key_buffer*, unsigned short);
int key_buffer_compare (struct key_buffer *haystack, struct key_buffer *needle);
void int_handler (int signum);
void exec_command (char *);
void update_descriptors_list (int **, int *);
int prepare_epoll (int *, int, int);
void str_to_argv (char ***, const char *);

// TODO: use getopts() to parse command line options
int main (void)
{
	/* Handle SIGINT */
	dead = 0;
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = int_handler;
	sigaction(SIGINT, &action, NULL);

	int fd_num = 0;
	int *fds = NULL;
	update_descriptors_list(&fds, &fd_num);

	int event_watcher = inotify_init1(IN_NONBLOCK);
	if (event_watcher < 0)
		die("could not call inotify_init");
	if (inotify_add_watch(event_watcher, evdev_root_dir, IN_CREATE | IN_DELETE) < 0)
		die("could not add /dev/input to the watch list");

	struct input_event event;
	struct key_buffer pb = {{0}, 0}; // Pressed keys buffer
	ssize_t rb; // Read bits

	/* Prepare for epoll */
	int ev_fd;
	ev_fd = prepare_epoll(fds, fd_num, event_watcher);

	/* MAIN EVENT LOOP */
	mainloop_begin:
	for (;;) {
		/* On linux use epoll(2) as it gives better performance */
		static struct epoll_event ev_type;
		if (epoll_wait(ev_fd, &ev_type, fd_num, -1) < 0 || dead)
			break;

		if (ev_type.events != EPOLLIN)
			continue;

		char buf[EVENT_BUF_LEN];
		if (read(event_watcher, buf, EVENT_BUF_LEN) >= 0) {
			sleep(1); // wait for devices to settle
			update_descriptors_list(&fds, &fd_num);
			if (close(ev_fd) < 0)
				die("could not close event filedescriptors list (ev_fd)");
			ev_fd = prepare_epoll(fds, fd_num, event_watcher);
			goto mainloop_begin;
		}

		static unsigned int prev_size;
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
				case (0):
					key_buffer_remove(&pb, event.code);
					break;
				/* Key pressed */
				case (1):
					key_buffer_add(&pb, event.code);
					break;
				}
			}
		}

		struct key_buffer comb1 = {{KEY_LEFTALT, KEY_S}, 2};

		if (pb.size != prev_size) {
			printf("Pressed keys: ");
			for (unsigned int i = 0; i < pb.size; i++)
				printf("%d ", pb.buf[i]);
			putchar('\n');

			if (key_buffer_compare(&pb, &comb1))
				exec_command("ufetch");
		}
	}

	// TODO: better child handling, for now all children receive the same
	// interrupts as the father so everything should work fine
	wait(NULL);
	if (!dead)
		fprintf(stderr, red("an error occured\n"));
	close(ev_fd);
	close(event_watcher);
	for (int i = 0; i < fd_num; i++)
		if (close(fds[i]) == -1)
			die("close file descriptors");
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

void int_handler (int signum)
{
	fprintf(stderr, yellow("Received interrupt signal, exiting gracefully...\n"));
	dead = 1;
}

void exec_command (char *path)
{
	char **argv = NULL;
	str_to_argv(&argv, path);

	switch (fork()) {
	case -1:
		die("Could not fork");
		break;
	case 0:
		/* we are the child */
		if (!argv) {
			printf(red("No command to execute\n"));
			exit(1);
		}
		if(execvp(path, argv) < 0) {
			/* execv only returns if an error occured, so we exit
			 * otherwise we duplicate the process */
			fprintf(stderr, red("Could not run %s\n"), path);
			exit(-1);
		}
		/* we shouldn't be here */
		break;
	}
	// TODO: communication between parent and child about process status/errors/etc
}

void update_descriptors_list (int **fds, int *fd_num)
{
	struct dirent *file_ent;
	char ev_path[sizeof(evdev_root_dir) + FILE_NAME_MAX_LENGTH + 1];
	void *tmp_p;
	int tmp_fd;
	unsigned char evtype_b[EV_MAX];
	/* Open the event directory */
	DIR *ev_dir = opendir(evdev_root_dir);
	if (!ev_dir)
		die("Could not open /dev/input");

	(*fd_num) = 0;
//	if ((*fds))
//		free(*fds);

	for (;;) {

		if ((file_ent = readdir(ev_dir)) == NULL)
			break;
		/* Filter out non character devices */
		if (file_ent->d_type != DT_CHR)
			continue;

		/* Compose absolute path from relative */
		strncpy(ev_path, evdev_root_dir, sizeof(evdev_root_dir) + FILE_NAME_MAX_LENGTH);
	   	strncat(ev_path, file_ent->d_name, sizeof(evdev_root_dir) + FILE_NAME_MAX_LENGTH);

		/* Open device and check if it can give key events otherwise ignore it */
		tmp_fd = open(ev_path, O_RDONLY | O_NONBLOCK);
		if (tmp_fd < 0) {
			fprintf(stderr, red("Could not open device %s\n"), ev_path);
			continue;
		}

		memset(evtype_b, 0, sizeof(evtype_b));
		if (ioctl(tmp_fd, EVIOCGBIT(0, EV_MAX), evtype_b) < 0) {
			fprintf(stderr, red("Could not read capabilities of device %s\n"),ev_path);
			close(tmp_fd);
			continue;
		}

		if (!test_bit(EV_KEY, evtype_b)) {
			fprintf(stderr, yellow("Ignoring device %s\n"), ev_path);
			close(tmp_fd);
			continue;
		}

		tmp_p = realloc((*fds), sizeof(int) * ((*fd_num) + 1));
		if (!tmp_p)
			die("realloc file descriptors");
		(*fds) = (int *) tmp_p;

		(*fds)[(*fd_num)] = tmp_fd;
		(*fd_num)++;
	}
	closedir(ev_dir);
	if (*fd_num) {
		fprintf(stderr, green("Monitoring %d devices\n"), *fd_num);
	} else {
		fprintf(stderr, red("Could not open any devices, exiting\n"));
		exit(-1);
	}
}

int prepare_epoll (int *fds, int fd_num, int event_watcher)
{
	static struct epoll_event epoll_read_ev;
 	epoll_read_ev.events = EPOLLIN;
 	int ev_fd = epoll_create(1);
 	if (ev_fd < 0)
 		die("failed epoll_create");
 	if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, event_watcher, &epoll_read_ev) < 0)
 		die("could not add file descriptor to the epoll list");
 	for (int i = 0; i < fd_num; i++)
 		if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, fds[i], &epoll_read_ev) < 0)
 			die("could not add file descriptor to the epoll list");
	return ev_fd;
}

int key_buffer_compare (struct key_buffer *haystack, struct key_buffer *needle)
{
	if (haystack->size != needle->size)
		return 0;
	int ff = 0;
	for (int x = needle->size - 1; x >= 0; x--) {
		for (int i = 0; i < haystack->size; i++)
			ff += (needle->buf[x] == haystack->buf[i]);
		if (!ff)
			return 0;
		ff = 0;
	}
	return 1;
}

void str_to_argv (char ***argv, const char *path)
{
	char * str = NULL;
	if (!(str = malloc(sizeof(path))))
		die("malloc in str_to_argv()");
	strcpy(str, path);

	char *token = NULL;
	token = strtok(str, " ");
	if (!token) {
		if (!(*argv = realloc(*argv, sizeof(char *))))
			die("realloc in str_to_argv()");
		*argv[0] = malloc(sizeof(str));
		strcpy(*argv[0], str);
		goto end_return;
	} else {
		if (!(*argv = realloc(*argv, sizeof(char *))))
			die("realloc in str_to_argv()");
		*argv[0] = malloc(sizeof(token));
		strcpy(*argv[0], token);
	}
	for (int i = 1; (token = strtok(NULL, " ")); i++) {
		if (!(*argv = realloc(*argv, sizeof(char *) * (i + 1))))
			die("realloc in str_to_argv()");
		*argv[i] = malloc(sizeof(token));
		strcpy(*argv[i], token);
	}
	end_return:
	free(str);
	return;
}
