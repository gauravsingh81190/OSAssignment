
xssh: xssh.o
	gcc -g xssh.o -o xssh

xssh.o: xssh.c
	gcc -g -c xssh.c -o xssh.o

clean:
	rm -rf xssh.o xssh

cscope:
	find -name "*.c" > files
	cscope -i files

