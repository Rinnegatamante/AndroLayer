TARGET := AndroLayer

all: $(TARGET).exe

LIBS = -lglfw3 -lkernel32 -lopengl32 -lglu32 -lgdi32 \
	-ldynarmic -lfmt -lmcl -lZydis -lopenal

OBJS = \
	dyn_util.o \
	glad/glad.o \
	main.o \
	port.o \
	pthread.o \
	so_util.o \
	variadics.o


CXXFLAGS = -fpermissive -std=c++20 
CFLAGS = -O3 -g

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=undefined
endif

ifeq ($(GDB_ENABLED),1)
CXXFLAGS += -DGDB_ENABLED
endif

ifeq ($(NO_DEBUG),1)
CXXFLAGS += -DNDEBUG
endif

ifeq ($(USE_INTERPRETER),1)
LIBS += -lunicorn.dll
CXXFLAGS += -DUSE_INTERPRETER
endif

CXXFLAGS += $(CFLAGS)

CXX = g++
CC = gcc

$(TARGET).exe: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@
	
clean:
	@rm -rf $(TARGET).exe $(OBJS)
