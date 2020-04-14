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

#ifdef __linux__
	#include <linux/input.h>
#endif 
#ifdef __FreeBSD__
	#include <dev/evdev/input.h>
#endif

struct pressed_buffer {
	unsigned short *buf;
	unsigned int size;
};

int term = 0; // Received SIGTERM flag

int pressBufferAdd (struct pressed_buffer*, unsigned short);
int pressBufferRemove (struct pressed_buffer*, unsigned short);
void termHandler (int signum);

// FIXME: use getopts() to parse commanfìd line options
int main (void)
{
	term = 0;
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = termHandler;
	sigaction(SIGINT, &action, NULL);

	struct pollfd fds[2];
	
	fds[0].events = POLLIN;
	// FIXME: test if option O_NONBLOCK has effects on performance
	fds[0].fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
	if (!fds[0].fd) {
		fputs(strerror(errno), stderr); 
		exit(errno);
	}
	
	fds[1].events = POLLIN;
	fds[1].fd = open("/dev/input/event3", O_RDONLY | O_NONBLOCK);
	if (!fds[1].fd) {
		fputs(strerror(errno), stderr); 
		exit(errno);
	}

	struct input_event event;
	struct pressed_buffer pb = {NULL, 0}; // Pressed keys buffer	
	ssize_t rb; // Read bits

	while (poll(fds, 2, -1) != -1 || !term) {
		/* Use poll(2) to wait por a file dscriptor to become ready for reading.
		 * NOTE: this could use select(2) but I don't know how */
		
		static int i;
		static int prev_size;
		
		prev_size = pb.size;
		for (i = 0; i < 2; i++) {
			if (fds[i].revents == fds[i].events) {

				rb = read(fds[i].fd, &event, sizeof(struct input_event));
				if (rb != sizeof(struct input_event)) continue;

				if (event.type == EV_KEY) {
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
	
		// FIXME: use fork and execl(3) to run the appropriate scripts
		if (pb.size != prev_size) {
			printf("Pressed keys: ");
			for (int i = 0; i < pb.size; i++)
				printf("%d ", pb.buf[i]);
			putchar('\n');
		}

	}

	if (!term)
		fputs("An error occured\n", stderr);
	for (int i = 0; i < 2; i++) {
			if (close(fds[i].fd) == -1) {
				fputs(strerror(errno), stderr);
				exit(errno);
			}
	}
	return 0;
}

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
		b = realloc(pb->buf, sizeof(unsigned short) * pb->size + 1);
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

void termHandler (int signum) {
	fputs("Received interrupt signal, exiting gracefully...\n", stderr);
	term = 1;
}
