all: stsound.a stympsp.elf

stsound.a:
	cd StSoundLibrary; \
	make -f makefile.psp; \
	cd ..

stympsp.elf:
	make -f makefile.psp

clean:
	make -f makefile.psp clean; \
	cd StSoundLibrary; \
	make -f makefile.psp clean; \
	cd ..
