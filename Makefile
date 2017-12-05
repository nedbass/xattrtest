xattrtest:
	gcc -o xattrtest -Wall -Werror xattrtest.c

checkstyle:
	git format-patch -k --stdout HEAD^ | ./checkpatch.pl --no-tree

clean:
	rm -f *.o
	rm -f xattrtest
