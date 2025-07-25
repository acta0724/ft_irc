MAKE := make $(MAKEFLAGS)
MAKEFLAGS += -rR
RM = rm -rf
CP = cp -r

CPPC = c++
CPPFLAGS = -Wall -Wextra -Werror -std=c++98 -pedantic -MMD -MP

NAME = ircserv
SRC_DIR = src
SRCS = src/main.cpp
OBJ_DIR = obj
OBJS = $(patsubst src/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
DEP_DIR = dep
DEPS = $(patsubst src/%.cpp, $(DEP_DIR)/%.d, $(SRCS))
HEADERS =

.PHONY: all
all: $(NAME)

.PHONY: clean
clean:
	$(RM) $(OBJ_DIR)
	$(RM) $(DEP_DIR)

.PHONY: fclean
fclean:
	$(MAKE) clean
	$(RM) $(NAME)

.PHONY: re
re:
	$(MAKE) fclean
	$(MAKE) all

.PHONY: run
run: $(NAME)
	./$(NAME)

$(NAME): $(OBJS)
	$(CPPC) $(CPPFLAGS) -o $@ $(OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(@D)
	$(CPPC) $(CPPFLAGS) -c -o $@ $<

-include $(DEPS)