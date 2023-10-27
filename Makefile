all:
	g++ -I. -I./snes_spc -I./demo \
    ./demo/play_spc.c \
    ./snes_spc/spc.cpp \
    ./snes_spc/SNES_SPC_misc.cpp \
    ./snes_spc/SNES_SPC.cpp \
    ./snes_spc/SPC_DSP.cpp \
    ./snes_spc/SNES_SPC_state.cpp \
    ./demo/wave_writer.c \
    ./demo/demo_util.c \
    ./snes_spc/SPC_Filter.cpp \
    -o Play


