build:
	gcc ./chip8_emu.c -lSDL3 -O3 -o ./chip8_emu
run:
	./chip8_emu $(FILE)
