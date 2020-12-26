terminal: terminal.c
	$(CC) -g terminal.c -o pretty_terminal -Wall -Wextra -pedantic -std=c99

clean:
	rm pretty_terminal