MAKE := MAKE $(MAKEFLAGS)
MAKEFLAGS += -rR
RM = rm -rf
CP = cp -r

CPPC = c++
CPPFLAGS = -Wall -Wextra -Werror -std=c++98 -pedantic -MMD -MP

NAME = ircserv
SRCS = main.cpp Client.cpp
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)
HEADERS = Client.hpp

DOCKER_COMPOSE = docker compose
DOCKERFILE = Dockerfile.txt
DOCKER_COMPOSE_FILE = docker-compose.yml

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

# Docker関連のターゲット
.PHONY: docker-build
docker-build:
	$(DOCKER_COMPOSE) build

.PHONY: docker-up
docker-up:
	$(DOCKER_COMPOSE) up -d

.PHONY: docker-down
docker-down:
	$(DOCKER_COMPOSE) down

.PHONY: docker-restart
docker-restart:
	$(DOCKER_COMPOSE) restart

.PHONY: docker-logs
docker-logs:
	$(DOCKER_COMPOSE) logs

.PHONY: docker-ps
docker-ps:
	$(DOCKER_COMPOSE) ps

.PHONY: docker-exec
docker-exec:
	$(DOCKER_COMPOSE) exec -it irc_server bash

.PHONY: docker-server
docker-server: all
	$(DOCKER_COMPOSE) exec irc_server bash -c "cd /ft_irc && ./$(NAME) 8080 irc_server"

.PHONY: docker-client
docker-client:
	$(DOCKER_COMPOSE) exec irc_client bash -c "nc -v irc_server 8080"

.PHONY: docker-all
docker-all: all docker-build docker-up

-include $(DEPS)