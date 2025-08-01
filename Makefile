all:
	gcc elfChecker.c -g -fsanitize=address -o elfChecker
clean:
	rm elfChecker
