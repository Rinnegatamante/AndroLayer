TARGET := AndroLayer

all: $(TARGET).exe

LIBS = -lglfw3 -lkernel32 -lopengl32 -lglu32 -lgdi32 \
	-ldynarmic -lfmt -lmcl -lZydis -lopenal

OBJS = \
	port.o \
	variadics.o \
	so_util.o \
	dyn_util.o \
	main.o \
	glad/glad.o

CXXFLAGS = -fpermissive -std=c++20 
CFLAGS = -O3 -g

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=undefined
endif

ifeq ($(NO_DEBUG),1)
CXXFLAGS += -DNDEBUG
endif

ifeq ($(USE_INTERPRETER),1)
LIBS += -lunicorn
CXXFLAGS += -DUSE_INTERPRETER
endif

CXXFLAGS += $(CFLAGS)

CXX = g++
CC = gcc

$(TARGET).exe: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@
	
clean:
	@rm -rf $(TARGET).exe $(OBJS)
