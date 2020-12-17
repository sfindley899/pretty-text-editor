terminal: terminal.c
	$(CC) terminal.c -o pretty_terminal -Wall -Wextra -pedantic -std=c99

clean:
	rm pretty_terminal