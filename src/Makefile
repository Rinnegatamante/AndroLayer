TARGET := AndroLayer

all: $(TARGET).exe

LIBS = -lglfw3 -lkernel32 -lopengl32 -lglu32 -lgdi32 \
	-ldynarmic -lfmt -lmcl -lZydis

OBJS = \
	port.o \
	so_util.o \
	main.o \
	glad/glad.o

CXXFLAGS = -O3 -fpermissive -g

CXX = g++
CC = gcc

$(TARGET).exe: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@
	
clean:
	@rm -rf $(TARGET).exe $(OBJS)
