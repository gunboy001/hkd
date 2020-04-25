/* Standard stuff */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Standard errors */
#include <errno.h>
/* Directory and file control */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
/* Polling */
#include <poll.h>
/* Signaling */
#include <signal.h>
/* Process wait */
#include <sys/wait.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define test_bit(yalv, abs_b) ((((char *)abs_b)[yalv/8] & (1<<yalv%8)) > 0)

/* Determine dependencies based on platform */
#ifdef __linux__
	#define OS linux
	#include <linux/input.h>
	#include <sys/epoll.h>
#endif
#ifdef __FreeBSD__
	#define OS bsd
	#include <dev/evdev/input.h>
#endif
#ifndef OS
	#define OS unix
#endif

struct key_buffer {
	unsigned short *buf;
	unsigned int size;
};

int term = 0; // exit flag
const char evdev_root_dir[] = "/dev/input/";

int key_buffer_add (struct key_buffer*, unsigned short);
int key_buffer_remove (struct key_buffer*, unsigned short);
int convert_key_value (unsigned short);
void int_handler (int signum);
void die (const char *, int);
void exec_command(char *);
void update_descriptors_list (struct pollfd **, int *);

// TODO: use getopts() to parse command line options
int main (void)
{
	/* Handle SIGINT */
	term = 0;
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = int_handler;
	sigaction(SIGINT, &action, NULL);

	int fd_num = 0;
	struct pollfd *fds = NULL;
	update_descriptors_list(&fds, &fd_num);

	if (!fd_num) {
		fputs("Could not open any device, exiting\n", stderr);
		exit(-1);
	}
	// TODO: watch for events inside /dev/input and reload accordingly
	// could use the epoll syscall or the inotify API (linux),
	// event API (openbsd), kqueue syscall (BSD and macos), a separate
	// process or some polling system inside the main loop to maintain
	// portability across other *NIX derivatives, could also use libev

	struct input_event event;
	struct key_buffer pb = {NULL, 0}; // Pressed keys buffer
	ssize_t rb; // Read bits

#if OS == linux
	struct epoll_event epoll_read_ev;
	epoll_read_ev.events = EPOLLIN;
	int ev_fd = epoll_create(1);
	if (ev_fd < 0)
		die("epoll_create", errno);
	for (int i = 0; i < fd_num; i++)
		if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, fds[i].fd, &epoll_read_ev) < 0)
			die("epoll_ctl", errno);
#endif 	/* Prepare for using epoll */

	for (;;) {

		// TODO: better error reporting
		/* On linux use epoll(2) as it gives better performance */
#if OS == linux
		static struct epoll_event ev_type;
		if (epoll_wait(ev_fd, &ev_type, fd_num, -1) == -1 || term)
			break;

		// TODO: use and test kqueue(2) for BSD systems
		/* On other systems use poll(2) to wait por a file dscriptor
		 * to become ready for reading. */
#else // TODO: add unix and bsd cases
		if (poll(fds, fd_num, -1) != -1 || term)
			break;
#endif

		static int i;
		static unsigned int prev_size;

		prev_size = pb.size;
		for (i = 0; i < fd_num; i++) {
#if OS == linux
			if (ev_type.events == EPOLLIN) {
#else // TODO: add unix and bsd cases
			if (fds[i].revents == fds[i].events) {
#endif

				rb = read(fds[i].fd, &event, sizeof(struct input_event));
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
	if (!term)
		fputs("An error occured\n", stderr);
	for (int i = 0; i < fd_num; i++) {
		if (close(fds[i].fd) == -1)
			die("close file descriptors", errno);
	}
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
		die("realloc failed in key_buffer_add", errno);
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
				die("realloc failed in key_buffer_remove: %s", errno);
			pb->buf = b;
			return 0;
		}
	}
	return 1;
}

void int_handler (int signum)
{
	fprintf(stderr, ANSI_COLOR_YELLOW
		"Received interrupt signal, exiting gracefully...\n" ANSI_COLOR_RESET);
	term = 1;
}

void die (const char *msg, int err)
{
	fprintf(stderr,
		ANSI_COLOR_RED "%s: %s" ANSI_COLOR_RESET,
		msg != NULL ? msg : "error", err ? strerror(err): "exiting");
	exit(err);
}

void exec_command (char *path)
{
	char *argv[] = {path, NULL};
	switch (fork()) {
	case -1:
		die("Could not fork: %s", errno);
		break;
	case 0:
		/* we are the child */
		if(execv(path, argv) < 0) {
			/* execv only returns if an error occured, so we exit
			 * otherwise we duplicate the process */
			fprintf(stderr, ANSI_COLOR_RED "Could not run %s\n" ANSI_COLOR_RESET
			, path);
			exit(-1);
		}
		/* we shouldn't be here */
		break;
	}
	// TODO: communication between parent and child about process status/errors/etc
}

void update_descriptors_list (struct pollfd **fds, int *fd_num)
{
	struct dirent *file_ent;
	char ev_path[sizeof(evdev_root_dir) + NAME_MAX + 1];
	void *tmp_p;
	int tmp_fd;
	unsigned char evtype_b[EV_MAX];
	/* Open the event directory */
	DIR *ev_dir = opendir(evdev_root_dir);
	if (!ev_dir)
		die("opendir", errno);

	(*fd_num) = 0;
	if ((*fds))
		free(fds);

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
			fprintf(stderr,
				ANSI_COLOR_RED "Could not open device %s\n"
				ANSI_COLOR_RESET, ev_path);
			continue;
		}

		memset(evtype_b, 0, sizeof(evtype_b));
		if (ioctl(tmp_fd, EVIOCGBIT(0, EV_MAX), evtype_b) < 0) {
			fprintf(stderr,ANSI_COLOR_RED
				"Could not read capabilities of device %s\n"
				ANSI_COLOR_RESET,ev_path);
			close(tmp_fd);
			continue;
		}

		if (!test_bit(EV_KEY, evtype_b)) {
			fprintf(stderr, ANSI_COLOR_YELLOW "Ignoring device %s\n"
				ANSI_COLOR_RESET, ev_path);
			close(tmp_fd);
			continue;
		}

		tmp_p = realloc((*fds), sizeof(struct pollfd) * ((*fd_num) + 1));
		if (!tmp_p)
			die("realloc file descriptors", errno);
		(*fds) = (struct pollfd *) tmp_p;

		(*fds)[(*fd_num)].events = POLLIN;
		(*fds)[(*fd_num)].fd = tmp_fd;
		(*fd_num)++;
	}
	fprintf(stderr,ANSI_COLOR_YELLOW "Monitoring %d devices\n" ANSI_COLOR_RESET, (*fd_num));
	closedir(ev_dir);
}

int convert_key_value (unsigned short key)
{
	int outchar = 0;
	return outchar;
}
