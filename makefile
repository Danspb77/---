all:
	gcc lab1sdsN3245.c -Wall -Wextra -Werror -o lab1sdsN3245 -O3
	gcc libsdsN3245.c -Wall -Wextra -Werror -fPIC -shared -ldl -lm -o libsdsN3245.so -O3

clean:
	rm -f lab12vdsN32451 libvdsN32451.so
