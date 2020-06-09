#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_RESET   "\x1b[0m"

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

		printf(ANSI_COLOR_RED "%s\n" ANSI_COLOR_RESET, line);
		/* remove white spaces */
		int inquotes = 0;
		int inapos = 0;
		for (size_t i = 0; i < linelen; i++) {
			if (line[i] == '"' && !inapos)
				inquotes = !inquotes;
			if (line[i] == '\'' && !inquotes)
				inapos = !inapos;

			if (isblank(line[i]) && !(inquotes || inapos)) {
				memmove(&line[i], &line[i + 1], linelen - i);
				i -= i ? 1 : 0;
			}
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
