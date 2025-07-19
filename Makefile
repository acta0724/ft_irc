MAKE := MAKE $(MAKEFLAGS)
MAKEFLAGS += -rR
RM = rm -rf
CP = cp -r

CPPC = c++
CPPFLAGS = -Wall -Wextra -Werror -std=c++98 -pedantic -MMD -MP

NAME = ircserv
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)
HEADERS =

.PHONY: all
all: $(NAME)

.PHONY: clean
clean:
	$(RM) $(OBJS)
	$(RM) $(DEPS)

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

%.o: %.cpp
	$(CPPC) $(CPPFLAGS) -c -o $@ $<

-include $(DEPS)