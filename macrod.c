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

// TODO: add children linked list structure and methods

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

	DIR *ev_dir = opendir(ev_root);
	if (!ev_dir) die();

	char ev_path[sizeof(ev_root) + NAME_MAX + 1];
	struct dirent *file_ent;
	void *tmp;
	int fd_num = 0;
	struct pollfd *fds = NULL;
	while ((file_ent = readdir(ev_dir)) != NULL) {
		if (file_ent->d_type == DT_CHR) {
				
			tmp = realloc(fds, sizeof(struct pollfd) * (fd_num + 1));
			if (!tmp) die();
			fds = tmp;
			
			strncpy(ev_path, ev_root, sizeof(ev_root) + NAME_MAX);
		   	strncat(ev_path, file_ent->d_name, sizeof(ev_root) + NAME_MAX);
			
			fds[fd_num].events = POLLIN;
			fds[fd_num].fd = open(ev_path, O_RDONLY | O_NONBLOCK);
			if (!fds[fd_num].fd) die();

			fd_num++;
		}
	}
	closedir(ev_dir);
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
	if (ev_fd == -1) die();
	for (int i = 0; i < fd_num; i++)
		if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, fds[i].fd, &epoll_read_ev) == -1)
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
		#elif OS == unix
		if (poll(fds, fd_num, -1) != -1 || term)
			break;
		#endif
		
		static int i;
		static int prev_size;
		
		prev_size = pb.size;
		for (i = 0; i < fd_num; i++) {
			#if OS == linux
			if (ev_type.events == EPOLLIN) {
			#elif OS == unix
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
			for (int i = 0; i < pb.size; i++)
				printf("%d ", pb.buf[i]);
			putchar('\n');
			if (pb.size == 2)
				if (pb.buf[0] == 56 || pb.buf[0] == 31)
					if (pb.buf[1] == 31 || pb.buf[1] == 56)
						execCommand("/home/ale/hello");
		}

	}

	// TODO: kill all running children before exiting
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
		for (int i = 0; i < pb->size; i++)
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
	
	for (int i = 0; i < pb->size; i++) {
		if (pb->buf[i] == key) {
			pb->size--;
			pb->buf[i] = pb->buf[pb->size];
			unsigned short *b;
			b = realloc(pb->buf, sizeof(unsigned short) * pb->size);
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
	pid_t child_pid = fork();
	if (child_pid == -1) die();
	// TODO: communication between parent and child about process status/errors/etc
	if (!child_pid) {
		/* we are the child */
		int ret = 0;
		ret = execl(path, path, (char *) NULL);
		if (ret != 0) 
				exit(-1);
	}
}
