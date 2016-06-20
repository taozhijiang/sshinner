DEBUG ?= 1
CC = gcc
CFLAGS = -g -O2
CPPFLAGS = 
CXX = g++
CXXFLAGS = $(CFLAGS)
PACKAGE = sshinner_server
PACKAGE_NAME = $(PACKAGE)
PACKAGE_STRING = $(PACKAGE_NAME)1.0
PACKAGE_VERSION = 1.0
SHELL = /bin/sh
VERSION = 1.0
SUBDIRS = source
COMMDIRS = ../common
TESTDIR = test
EXTRAFLAGS = -g -I./include -I../include -lpthread -lrt -lssl -lcrypto -levent -lsystemd -ljson-c -lz
OBJDIR = obj

vpath %.c $(SUBDIRS)
vpath %.c $(COMMDIRS)
vpath %.c $(TESTDIR)


srcs = $(filter-out client.c, $(filter-out server.c, $(notdir $(wildcard $(SUBDIRS)/*.c))))
commons = $(filter-out main.c, $(notdir $(wildcard $(COMMDIRS)/*.c)))

objs = $(srcs:%.c=$(OBJDIR)/%.o)
common_objs = $(commons:%.c=$(OBJDIR)/%.o)

test_srcs = $(notdir $(wildcard $(TESTDIR)/*.c))
test_objs = $(test_srcs:%.c=$(OBJDIR)/%.o)
test_exec = $(test_srcs:%.c=%)

ifeq ($(DEBUG),1)
	TARGET_DIR=Debug
else
	TARGET_DIR=Release
endif

all : $(PACKAGE)
.PHONY : all
.PHONY : test

$(PACKAGE) : $(objs) $(common_objs)
	$(CC) -c $(CCFLAGS) $(EXTRAFLAGS) $(SUBDIRS)/server.c -o $(OBJDIR)/server.o
	$(CC) $(CCFLAGS) $(objs) $(common_objs) $(OBJDIR)/server.o $(EXTRAFLAGS) -o $(TARGET_DIR)/$(PACKAGE)

$(objs) : $(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	$(CC) -MMD -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@ 

$(common_objs) : $(OBJDIR)/%.o: %.c
	$(CC) -MMD -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@ 

#check header for obj reconstruction
-include $(OBJDIR)/*.d

test : $(test_objs)
	$(foreach test_target, $(test_exec), $(CC) $(CCFLAGS) $(EXTRAFLAGS) $(objs) -o $(test_target)  $(OBJDIR)/$(test_target).o ;)	
$(test_objs) : $(OBJDIR)/%.o: %.c
	$(CC) -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@ 

.PHONY : clean 
clean :	
	-rm -fr $(OBJDIR)/* $(TARGET_DIR)/*
	-rm -fr $(test_exec) 
