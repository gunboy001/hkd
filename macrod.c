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

int pressBufferAdd (struct pressed_buffer*, unsigned short);
int pressBufferRemove (struct pressed_buffer*, unsigned short);

int main (void) // remember getopt() to automaically parse options
{
	int fd;
	fd = open("/dev/input/event0", O_RDONLY); // TEST: O_NONBLOCK
	if (!fd) {
		fputs(strerror(errno), stderr); 
		exit(errno);
	}

	//int f2 = open("/dev/input/event3", O_RDONLY);
	//struct input_event ev;

	struct input_event event;
	struct pressed_buffer pb = {NULL, 0}; // Pressed keys buffer	

	while (1) {
		/* Use polling poll(2) to wait por a file dscriptor to become ready for reading.
		 * NOTE: this could use select(2) but I don't know how */
		
		//fread(&event, sizeof(struct input_event), 1, fp);

		ssize_t rb;
		rb = read(fd, &event, sizeof(struct input_event));
		if (rb != sizeof(struct input_event)) continue;

		if (event.type == EV_KEY) {
			
			switch (event.value) {
				case (0): // Key release
					pressBufferRemove(&pb, event.code);
					break;
				case (1): // Key press
					pressBufferAdd(&pb, event.code);
					break;
			}

			printf("Pressed keys: ");
			for (int i = 0; i < pb.size; i++)
				printf("%d ", pb.buf[i]);
			putchar('\n');
		}
	}

	if (close(fd) == -1) {
		fputs(strerror(errno), stderr);
		exit(errno);
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
