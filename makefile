all:
	gcc lab12vdsN32451.c -Wall -Wextra -Werror -o lab12vdsN32451 -O3
	gcc libvdsN32451.c -Wall -Wextra -Werror -fPIC -shared -ldl -lm -o libvdsN32451.so -O3

clean:
	rm -f lab12vdsN32451 libvdsN32451.so
