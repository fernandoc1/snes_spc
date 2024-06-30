# Directory containing the source files
SRCDIR := ./snes_spc
# Directory to place the object files
OBJDIR := ./obj

# Get all .c files in the source directory
CFILES := $(wildcard $(SRCDIR)/*.cpp)
# Convert .c files to .o files
OFILES := $(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(CFILES))

# Target to build all object files
all: clean $(OFILES) portaudio

# Rule to compile each .c file to .o file
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OBJDIR)
	g++ -g -c $< -o $@
	#g++ -E -c $< | grep -v "^#" | grep -v -e '^[[:space:]]*$$' > $@.cpp
	#g++ -g -c $@.cpp -o $@

portaudio:
	g++ -g demo/port_audio_player.cpp \
    -I. -I./snes_spc -I./demo \
    $(OBJDIR)/*.o \
    ./demo/demo_util.c \
    -lportaudio -o PortAudioPlayer


# A phony target to clean up
.PHONY: clean
clean:
	@echo Cleaning up...
	rm -rf $(OBJDIR)
	rm PortAudioPlayer

