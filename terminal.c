#include <unistd.h>

int main() 
{
	char c;
	// loop to read 1 byte from standard input
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
	{
		/* */
	}

	return 0;
}