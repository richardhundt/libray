all:
	make -C ./src

clean:
	make -C ./src clean
	rm -f *.so

test:
	make -C ./test

realclean:
	make -C ./src realclean

.PHONY: all clean realclean test
