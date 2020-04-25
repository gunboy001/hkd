#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int main(int argc, char const *argv[])
{
	if (argc < 2)
		return 1;
	FILE *fd;
	fd = fopen(argv[1], "r");
	if (!fd)
		return -1;
	for (;;) {
		char *line = NULL;
		size_t linelen = 0;
		if (getline(&line, &linelen, fd) == -1)
			break;
		if (linelen < 2)
			continue;

		printf("%s\n", line);
		// remove white spaces
		for (size_t i = 0; i < linelen; i++) {
			if (isblank(line[i]))
				memmove(&line[i], &line[i + 1], linelen - i);
		}
		if (line[0] == '#')
			continue;
		printf("%s\n", line);

		char *token = NULL;
		token = strtok(line, "=");
		if (token)
			printf("%s\n", token);
		token = strtok(NULL, "=");
		if (token)
			printf("%s\n", token);
		free(line);
	}
	return 0;
}
