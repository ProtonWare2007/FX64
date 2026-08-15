#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <SDL3/SDL.h>

typedef struct {
    uint16_t stack[16];
    uint8_t stackPointer;
} CallStack;

const uint8_t fonts[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

uint8_t Ram[4096];
uint8_t VRam[2048]; //Resolution:64*32
uint8_t VRegisters[16];
bool keyPad[16] = {false};
CallStack Stack;
uint16_t IReg;
uint16_t PC = 0x200;
uint8_t DelayTMR;
uint8_t SoundTMR;
double emuTimer;

bool isRunning = true;
uint8_t scale = 14;

SDL_Surface* buffer;

void readFile(const char* path)
{
    FILE* filep = fopen(path, "rb");
    if(filep == NULL)
    {
        fprintf(stderr, "%s\n", "unable to open file, consider checking the path to the ROM file");
        exit(64);
    }
    fseek(filep, 0, SEEK_END);
    long fsize = ftell(filep);
    rewind(filep);
    fread(Ram+0x200, sizeof(char), fsize, filep);
    fclose(filep);
}

void drawSprite()
{
    uint8_t x, y, nBytes, value;
    uint16_t Opcode = (Ram[PC] << 8) + Ram[PC+1];
    x = VRegisters[(Opcode & 0x0F00) >> 8] % 64;
    y = VRegisters[(Opcode & 0x00F0) >> 4] % 32;
    nBytes = Opcode & 0x000F;
    VRegisters[15] = 0x00;
    for(uint8_t n = 0;n < nBytes;n++)
        for(uint8_t i = 8; i > 0;i--) {
            if(x+8-i > 63 || (y+n) > 31) continue;
            value = (Ram[IReg+n] & (0x01 << (i-1))) >> (i-1);
            if(value && VRam[x+8-i+64*(y+n)]) VRegisters[15] = 0x01;
            VRam[x+8-i+64*(y+n)] ^= value;
        }
    uint8_t colorValue;
    for(uint8_t y = 0; y < 32;y++) {
        for(uint8_t x = 0; x < 64;x++) {
            colorValue = VRam[x+y*64]*255;
            SDL_WriteSurfacePixel(buffer, x, y, colorValue, colorValue, colorValue, 255);
        }
    }
}

void execAluOperation(uint16_t Opcode) {
    uint16_t value, value2;
    switch (Opcode & 0x000F) {
        case 0x0000:
            VRegisters[(Opcode & 0x0F00) >> 8] = VRegisters[(Opcode & 0x00F0) >> 4];
            break;
        case 0x0001:
            VRegisters[(Opcode & 0x0F00) >> 8] |= VRegisters[(Opcode & 0x00F0) >> 4];
            break;
        case 0x0002:
            VRegisters[(Opcode & 0x0F00) >> 8] &= VRegisters[(Opcode & 0x00F0) >> 4];
            break;
        case 0x0003:
            VRegisters[(Opcode & 0x0F00) >> 8] ^= VRegisters[(Opcode & 0x00F0) >> 4];
            break;
        case 0x0004:
            value = VRegisters[(Opcode & 0x0F00) >> 8] + VRegisters[(Opcode & 0x00F0) >> 4];
            if(value > 255) VRegisters[15] = 1;
            else VRegisters[15] = 0;
            VRegisters[(Opcode & 0x0F00) >> 8] = value;
            break;
        case 0x0005:
            value = (Opcode & 0x0F00) >> 8;
            value2 = VRegisters[(Opcode & 0x00F0) >> 4];
            uint8_t temp = VRegisters[value] >= value2 ? 0x01 : 0x00;
            VRegisters[value] -= value2;
            VRegisters[15] = temp;
            break;
        case 0x0006:
            value = (Opcode & 0x0F00) >> 8;
            VRegisters[15] = VRegisters[value] & 0b00000001;
            VRegisters[value] >>= 1;
            break;
        case 0x0007:
            value = (Opcode & 0x0F00) >> 8;
            value2 = VRegisters[(Opcode & 0x00F0) >> 4];
            if(value2 >= VRegisters[value]) VRegisters[15] = 0x01;
            else VRegisters[15] = 0x00;
            VRegisters[value] = value2 - VRegisters[value];
            break;
        case 0x000E:
            value = (Opcode & 0x0F00) >> 8;
            VRegisters[15] = (VRegisters[value] & 0b10000000) >> 7;
            VRegisters[value] <<= 1;
            break;
    }
}

uint8_t convertKey(uint8_t keyCode) {
    switch (keyCode) {
        case SDL_SCANCODE_1:
            return 0x01;
            break;
        case SDL_SCANCODE_2:
            return 0x02;
            break;
        case SDL_SCANCODE_3:
            return 0x03;
            break;
        case SDL_SCANCODE_4:
            return 0x0C;
            break;
        case SDL_SCANCODE_Q:
            return 0x04;
            break;
        case SDL_SCANCODE_W:
            return 0x05;
            break;
        case SDL_SCANCODE_E:
            return 0x06;
            break;
        case SDL_SCANCODE_R:
            return 0x0D;
            break;
        case SDL_SCANCODE_A:
            return 0x07;
            break;
        case SDL_SCANCODE_S:
            return 0x08;
            break;
        case SDL_SCANCODE_D:
            return 0x09;
            break;
        case SDL_SCANCODE_F:
            return 0x0E;
            break;
        case SDL_SCANCODE_Z:
            return 0x0A;
            break;
        case SDL_SCANCODE_X:
            return 0x00;
            break;
        case SDL_SCANCODE_C:
            return 0x0B;
            break;
        case SDL_SCANCODE_V:
            return 0x0F;
            break;
        default:
            return 0xFF;
    }
}

void execFOpcodes(uint16_t Opcode) {
    uint8_t value1, value2;
    switch (Opcode & 0x00F0) {
        case 0x0000:
            if ((Opcode & 0x000F) == 0x0007) {
                VRegisters[(Opcode & 0x0F00) >> 8] = DelayTMR;
            } else if ((Opcode & 0x000F) == 0x000A) {
                bool foundKey = false;
                for(uint8_t i = 0; i < 16;i++) {
                    if(keyPad[i]) {
                        VRegisters[(Opcode & 0x0F00) >> 8] = i;
                        foundKey = true;
                        break;
                    }
                }
                if(!foundKey) PC -= 2;
            } else {
                fprintf(stderr, "%s\n", "unknown instruction1");
                exit(1);
            }
            break;
        case 0x0010:
            if ((Opcode & 0x000F) == 0x000E) {
                IReg += VRegisters[(Opcode & 0x0F00) >> 8];
            } else if ((Opcode & 0x000F) == 0x0005) {
                DelayTMR = VRegisters[(Opcode & 0x0F00) >> 8];
            } else if ((Opcode & 0x000F) == 0x0008) {
                SoundTMR = VRegisters[(Opcode & 0x0F00) >> 8];
            } else {
                fprintf(stderr, "%s\n", "unknown instruction");
                exit(1);
            }
            break;
        case 0x0020:
            if ((Opcode & 0x000F) == 0x0009) {
                IReg = VRegisters[(Opcode & 0x0F00) >> 8] * 5 + 0x050;
            } else {
                fprintf(stderr, "%s\n", "unknown instruction");
                exit(1);
            }
            break;
        case 0x0030:
            if ((Opcode & 0x000F) == 0x0003) {
                value1 = VRegisters[(Opcode & 0x0F00) >> 8];
                value2 = value1 % 100;
                Ram[IReg] = value1 / 100;
                Ram[IReg + 1] = value2 / 10;
                Ram[IReg + 2] = value2 % 10;
            } else {
                fprintf(stderr, "%s\n", "unknown instruction");
                exit(1);
            }
            break;
        case 0x0050:
            if ((Opcode & 0x000F) == 0x0005) {
                value1 = (Opcode & 0x0F00) >> 8;
                for(uint8_t i = 0; i <= value1;i++)
                    Ram[IReg + i] = VRegisters[i];
                //++IReg;
            } else {
                fprintf(stderr, "%s\n", "unknown instruction");
                exit(1);
            }
            break;
        case 0x0060:
            if ((Opcode & 0x000F) == 0x0005) {
                value1 = (Opcode & 0x0F00) >> 8;
                for(uint8_t i = 0; i <= value1;i++)
                    VRegisters[i] = Ram[IReg + i];
                //++IReg;
            } else {
                fprintf(stderr, "%s\n", "unknown instruction");
                exit(1);
            }
            break;
        default:
            fprintf(stderr, "%s\n", "unknown instruction");
            exit(1);
    }
}

void execEOpcodes(uint16_t Opcode) {
    uint8_t value = (Opcode & 0x0F00) >> 8;
    uint8_t key = VRegisters[value];
    switch (Opcode & 0x00FF) {
        case 0x009E:
            if (key <= 0x0F && keyPad[key]) PC += 2;
            break;
        case 0x00A1:
            if (key <= 0x0F && !keyPad[key]) PC += 2;
            break;
        default:
            fprintf(stderr, "%s\n", "unknown instruction");
            exit(1);
    }
}

void execute() {
    uint16_t Opcode = 0x0000;
    uint16_t MSN;
    double now = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    if (DelayTMR > 0 && (now - emuTimer) * 60 >= 1.0) {
        emuTimer = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
        --DelayTMR;
    }
    if (SoundTMR > 0 && (now - emuTimer) * 60 >= 1.0) {
        emuTimer = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
        //Beep
        --SoundTMR;
    }
    if (PC < 0x1000) {
        Opcode = (Ram[PC] << 8) + Ram[PC+1];
        MSN = Opcode & 0xF000;
        if (Opcode == 0x00E0) {
            for(uint16_t i = 0;i < 2048;i++) VRam[i] = 0;
            SDL_FillSurfaceRect(buffer, NULL, 0x00000000);
        } else if(Opcode == 0x00EE) {
                PC = Stack.stack[--Stack.stackPointer];
                return;
        } else if (MSN == 0x1000) {
            PC = Opcode & 0x0FFF;
            return;
        } else if (MSN == 0x2000) {
            Stack.stack[Stack.stackPointer++] = PC+2;
            PC = Opcode & 0x0FFF;
            return;
        } else if (MSN == 0x3000) {
            if (VRegisters[(Opcode & 0x0F00) >> 8] == (Opcode & 0x00FF)) {
                PC += 4;
                return;
            }
        } else if (MSN == 0x4000) {
            if (VRegisters[(Opcode & 0x0F00) >> 8] != (Opcode & 0x00FF)) {
                PC += 4;
                return;
            }
        } else if (MSN == 0x5000) {
            if (VRegisters[(Opcode & 0x0F00) >> 8] == VRegisters[(Opcode & 0x00F0) >> 4]) {
                PC += 4;
                return;
            }
        } else if (MSN == 0x6000) {
            VRegisters[(Opcode & 0x0F00) >> 8] = Opcode & 0x00FF;
        } else if (MSN == 0x7000) {
            VRegisters[(Opcode & 0x0F00) >> 8] += Opcode & 0x00FF;
        } else if (MSN == 0x8000) {
            execAluOperation(Opcode);
        } else if (MSN == 0x9000) {
            if(VRegisters[(Opcode & 0x0F00) >> 8] != VRegisters[(Opcode & 0x00F0) >> 4]) {
                PC += 4;
                return;
            }
        } else if (MSN == 0xA000) {
            IReg = Opcode & 0x0FFF;
        } else if (MSN == 0xB000) {
            PC = (Opcode & 0x0FFF) + VRegisters[0];
            return;
        } else if (MSN == 0xC000) {
            VRegisters[(Opcode & 0x0F00) >> 8] = (rand() % 256) & (Opcode & 0x00FF);
        } else if (MSN == 0xD000) {
            drawSprite();
        } else if (MSN == 0xE000) {
            execEOpcodes(Opcode);
        } else if (MSN == 0xF000) {
            execFOpcodes(Opcode);
        } else {
            fprintf(stderr, "%s\n", "unknown instruction");
            exit(1);
        }
        PC += 2;
    }
}

void start() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Window* win =  SDL_CreateWindow("CHIP-8 Emulator", 64*scale, 32*scale, 0);
    SDL_Surface* win_surf = SDL_GetWindowSurface(win);
    buffer = SDL_CreateSurface(64, 32, SDL_PIXELFORMAT_RGB24);
    SDL_Surface* scaledBuffer;
    SDL_Event event;
    uint8_t colorValue;
    emuTimer = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    while (isRunning) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) goto Quit;
            else if (event.type == SDL_EVENT_KEY_DOWN) {
                uint8_t key = convertKey(event.key.scancode);
                if(key != 0xFF) keyPad[key] = true;
            } else if (event.type == SDL_EVENT_KEY_UP) {
                uint8_t key = convertKey(event.key.scancode);
                if(key != 0xFF) keyPad[key] = false;
            }
        }
        execute();
        scaledBuffer = SDL_ScaleSurface(buffer, 64 * scale, 32 * scale, SDL_SCALEMODE_NEAREST);
        //SDL_BlitSurfaceScaled(buffer, NULL, win_surf, NULL, SDL_SCALEMODE_NEAREST); Slow!!!
        SDL_BlitSurface(scaledBuffer, NULL, win_surf, NULL);
        SDL_DestroySurface(scaledBuffer);
        SDL_UpdateWindowSurface(win);
    }
    Quit:
        SDL_DestroyWindow(win);
        SDL_Quit();
}

void loadFont() {
    for(uint8_t i = 0; i < 80;i++) {
        Ram[0x050 + i] = fonts[i];
    }
}

int main(int argc, char** argv) {
    srand(time(NULL));
    if(argc == 2)
    {
        readFile(argv[1]);
        loadFont();
        start();
    } else {
     printf("%s\n", "<usage>:chip8_emu path_to_file");
    }
    return 0;
}
