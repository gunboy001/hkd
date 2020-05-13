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

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define EVENT_BUF_LEN     (1024 * ( EVENT_SIZE + 16 ))

struct key_buffer {
	unsigned short *buf;
	unsigned int size;
};

int dead = 0; // exit flag
const char evdev_root_dir[] = "/dev/input/";

int key_buffer_add (struct key_buffer*, unsigned short);
int key_buffer_remove (struct key_buffer*, unsigned short);
int convert_key_value (unsigned short);
void int_handler (int signum);
void exec_command(char *);
void update_descriptors_list (int **, int *);
int prepare_epoll(int *, int, int);

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
	struct key_buffer pb = {NULL, 0}; // Pressed keys buffer
	ssize_t rb; // Read bits

	/* Prepare for epoll */
	int ev_fd;
	ev_fd = prepare_epoll(fds, fd_num, event_watcher);

	/* MAIN EVENT LOOP */
	mainloop_begin:
	for (;;) {
		// TODO: better error reporting
		/* On linux use epoll(2) as it gives better performance */
		static struct epoll_event ev_type;
		if (epoll_wait(ev_fd, &ev_type, fd_num, -1) < 0 || dead)
			break;

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
		if (ev_type.events == EPOLLIN) {
			for (int i = 0; i < fd_num; i++) {

				rb = read(fds[i], &event, sizeof(struct input_event));
				if (rb != sizeof(struct input_event)) continue;

				/* Ignore touchpad events */
				// TODO: make a event blacklist system
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
		}

		if (pb.size != prev_size) {
			printf("Pressed keys: ");
			for (unsigned int i = 0; i < pb.size; i++)
				printf("%d ", pb.buf[i]);
			putchar('\n');
			switch (pb.size) {
			case 1:
				/* You can use keys defined in input.h */
				if (pb.buf[0] == KEY_MUTE)
					exec_command((char *)"/home/ale/hello");
				break;
			case 2:
				if (pb.buf[0] == 56 || pb.buf[0] == 31)
					if (pb.buf[1] == 31 || pb.buf[1] == 56)
						exec_command((char *)"/home/ale/hello");
				break;
			}
		}

	}

	// TODO: better child handling, for now all children receive the same
	// interrupts as the father so everything should work fine for now
	wait(NULL);
	free(pb.buf);
	if (!dead)
		fprintf(stderr, red("an error occured\n"));
	close(ev_fd);
	close(event_watcher);
	for (int i = 0; i < fd_num; i++)
		if (close(fds[i]) == -1)
			die("close file descriptors");
	return 0;
}

// TODO: optimize functions to preallocate some memory
int key_buffer_add (struct key_buffer *pb, unsigned short key)
{
/* Adds a keycode to the pressed buffer if it is not already present
 * Returns non zero if the key was not added. */

	if (!pb) return 1;
	if (pb->buf != NULL) {
		/* Linear search if the key is already buffered */
		for (unsigned int i = 0; i < pb->size; i++)
			if (key == pb->buf[i]) return 1;
	}

	unsigned short *b;
		b = (unsigned short *) realloc(pb->buf, sizeof(unsigned short) * (pb->size + 1));
	if (!b)
		die("realloc failed in key_buffer_add");
	pb->buf = b;
	pb->buf[pb->size++] = key;

	return 0;
}

int key_buffer_remove (struct key_buffer *pb, unsigned short key)
{
/* Removes a keycode from a pressed buffer if it is present returns
 * non zero in case of failure (key not present or buffer empty). */
	if (!pb) return 1;

	for (unsigned int i = 0; i < pb->size; i++) {
		if (pb->buf[i] == key) {
			pb->size--;
			pb->buf[i] = pb->buf[pb->size];
			unsigned short *b;
			b = (unsigned short *) realloc(pb->buf, sizeof(unsigned short) * pb->size);
			/* if realloc failed but the buffer is populated throw an error */
			if (!b && pb->size)
				die("realloc failed in key_buffer_remove: %s");
			pb->buf = b;
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
	char *argv[] = {path, NULL};
	switch (fork()) {
	case -1:
		die("Could not fork: %s");
		break;
	case 0:
		/* we are the child */
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
	char ev_path[sizeof(evdev_root_dir) + NAME_MAX + 1];
	void *tmp_p;
	int tmp_fd;
	unsigned char evtype_b[EV_MAX];
	/* Open the event directory */
	DIR *ev_dir = opendir(evdev_root_dir);
	if (!ev_dir)
		die("Could not open /dev/input");

	(*fd_num) = 0;
	if ((*fds))
		free(*fds);

	for (;;) {

		if ((file_ent = readdir(ev_dir)) == NULL)
			break;
		/* Filter out non character devices */
		if (file_ent->d_type != DT_CHR)
			continue;

		/* Compose absolute path from relative */
		strncpy(ev_path, evdev_root_dir, sizeof(evdev_root_dir) + NAME_MAX);
	   	strncat(ev_path, file_ent->d_name, sizeof(evdev_root_dir) + NAME_MAX);

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

int convert_key_value (unsigned short key)
{
	int outchar = 0;
	return outchar;
}

int prepare_epoll(int *fds, int fd_num, int event_watcher)
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
