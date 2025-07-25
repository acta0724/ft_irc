MAKE := make $(MAKEFLAGS)
MAKEFLAGS += -rR
RM = rm -rf
CP = cp -r

CPPC = c++
CPPFLAGS = -Wall -Wextra -Werror -std=c++98 -pedantic -MMD -MP -Iinc

NAME = ircserv
SRC_DIR = src
SRCS = src/main.cpp src/Client.cpp
OBJ_DIR = obj
OBJS = $(patsubst src/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
DEPS = $(patsubst src/%.cpp, $(OBJ_DIR)/%.d, $(SRCS))
HEADERS =

.PHONY: all
all: $(NAME) ## 実行ファイルの作成

.PHONY: clean
clean: ## 中間ファイルのクリーンアップ
	$(RM) $(OBJ_DIR)

.PHONY: fclean
fclean: ## 完全なクリーンアップ
	$(MAKE) clean
	$(RM) $(NAME)

.PHONY: re
re: ## 再コンパイル
	$(MAKE) fclean
	$(MAKE) all

.PHONY: run
run: $(NAME) ## プログラムの実行
	./$(NAME) 8080 "pass"

.PHONY: test
test: $(NAME) ## テストの実行
	python3 test/test_irc_server.py

.PHONY: help
help: ## ヘルプ
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' Makefile | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

$(NAME): $(OBJS)
	$(CPPC) $(CPPFLAGS) -o $@ $(OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(@D)
	$(CPPC) $(CPPFLAGS) -c -o $@ $<

-include $(DEPS)

