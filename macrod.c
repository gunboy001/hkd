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

struct pressed_buffer {
	unsigned short *buf;
	unsigned int size;
};

int term = 0; // exit flag
const char ev_root[] = "/dev/input/";

int pressBufferAdd (struct pressed_buffer*, unsigned short);
int pressBufferRemove (struct pressed_buffer*, unsigned short);
void termHandler (int signum);
void die (void);
void execCommand(const char *);

// TODO: use getopts() to parse command line options
int main (void)
{
	/* Handle SIGINT */
	term = 0;
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = termHandler;
	sigaction(SIGINT, &action, NULL);

	/* Open the event directory */
	DIR *ev_dir = opendir(ev_root);
	if (!ev_dir)
		die();

	int fd_num = 0;
	struct pollfd *fds = NULL;

	for (;;) {
		struct dirent *file_ent;
		char ev_path[sizeof(ev_root) + NAME_MAX + 1];
		void *tmp_p;
		int tmp_fd;
		unsigned char evtype_b[EV_MAX];

		if ((file_ent = readdir(ev_dir)) == NULL)
			break;
		/* Filter out non character devices */
		if (file_ent->d_type != DT_CHR)
			continue;

		/* Compose absolute path from relative */
		strncpy(ev_path, ev_root, sizeof(ev_root) + NAME_MAX);
	   	strncat(ev_path, file_ent->d_name, sizeof(ev_root) + NAME_MAX);

		/* Open device and check if it can give key events otherwise ignore it */
		tmp_fd = open(ev_path, O_RDONLY | O_NONBLOCK);
		if (tmp_fd < 0) {
			fprintf(stderr, "Could not open device %s\n", ev_path);
			continue;
		}

		memset(evtype_b, 0, sizeof(evtype_b));
		if (ioctl(tmp_fd, EVIOCGBIT(0, EV_MAX), evtype_b) < 0) {
			fprintf(stderr, "Could not read capabilities of device %s\n",
				ev_path);
			close(tmp_fd);
			continue;
		}

		if (!test_bit(EV_KEY, evtype_b)) {
			fprintf(stderr, "Ignoring device %s\n", ev_path);
			close(tmp_fd);
			continue;
		}

		tmp_p = realloc(fds, sizeof(struct pollfd) * (fd_num + 1));
		if (!tmp_p)
			die();
		fds = tmp_p;

		fds[fd_num].events = POLLIN;
		fds[fd_num].fd = tmp_fd;

		fd_num++;
	}
	closedir(ev_dir);
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
	struct pressed_buffer pb = {NULL, 0}; // Pressed keys buffer
	ssize_t rb; // Read bits

	/* Prepare for using epoll */
	#if OS == linux
	struct epoll_event epoll_read_ev;
	epoll_read_ev.events = EPOLLIN;
	int ev_fd = epoll_create(1);
	if (ev_fd < 0)
			die();
	for (int i = 0; i < fd_num; i++)
		if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, fds[i].fd, &epoll_read_ev) < 0)
			die();
	#endif

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
							pressBufferRemove(&pb, event.code);
							break;
						/* Key pressed */
						case (1):
							pressBufferAdd(&pb, event.code);
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
			if (pb.size == 2)
				if (pb.buf[0] == 56 || pb.buf[0] == 31)
					if (pb.buf[1] == 31 || pb.buf[1] == 56)
						execCommand("/home/ale/hello");
		}

	}

	// TODO: better child handling, for now all children receive the same
	// interrupts as the father so everything should work fine for now
	wait(NULL);
	free(pb.buf);
	if (!term)
		fputs("An error occured\n", stderr);
	for (int i = 0; i < fd_num; i++) {
			if (close(fds[i].fd) == -1) die();
	}
	return 0;
}

// TODO: optimize functions to preallocate some memory
int pressBufferAdd (struct pressed_buffer *pb, unsigned short key)
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
		b = realloc(pb->buf, sizeof(unsigned short) * (pb->size + 1));
	if (!b) {
		fprintf(stderr, "realloc failed in pressBufferAdd: %s", strerror(errno));
		exit(errno);
	}
	pb->buf = b;
	pb->buf[pb->size++] = key;

	return 0;
}

int pressBufferRemove (struct pressed_buffer *pb, unsigned short key)
{
/* Removes a keycode from a pressed buffer if it is present returns
 * non zero in case of failure (key not present or buffer empty). */
	if (!pb) return 1;

	for (unsigned int i = 0; i < pb->size; i++) {
		if (pb->buf[i] == key) {
			pb->size--;
			pb->buf[i] = pb->buf[pb->size];
			unsigned short *b;
			b = realloc(pb->buf, sizeof(unsigned short) * pb->size);
			/* if realloc failed but the buffer is populated throw an error */
			if (!b && pb->size) {
				fprintf(stderr, "realloc failed in pressBufferRemove: %s",
					strerror(errno));
				exit(errno);
			}
			pb->buf = b;
			return 0;
		}
	}
	return 1;
}

void termHandler (int signum)
{
	fputs("Received interrupt signal, exiting gracefully...\n", stderr);
	term = 1;
}

void die (void)
{
	fputs(strerror(errno), stderr);
	exit(errno);
}

void execCommand (const char *path)
{
	switch (fork()) {
		case -1:
			fprintf(stderr, "Could not fork: %s", strerror(errno));
			break;
		case 0:
			/* we are the child */
			if(execl(path, path, (char *) NULL) != 0)
				/* execl only returns if an error occured, so we exit
				 * otherwise we duplicate the process */
				exit(-1);
			/* we shouldn't be here */
			break;
	}
	// TODO: communication between parent and child about process status/errors/etc
}
